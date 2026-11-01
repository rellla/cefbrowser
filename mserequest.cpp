#include <cstring>
#include <thread>
#include <mutex>
#include <vector>
#include "mserequest.h"
#include "logger.h"
#include "thrift-services/src-client/TranscoderClient.h"

#define MSE_PATH "/__vdrmse__/"

// one connection for all requests, they are sent one after another
static std::mutex transcoderMutex;
static TranscoderClient *transcoderClient = nullptr;

MseRequest::MseRequest(const BrowserParameter &bParam) : bParam(bParam), status(500) {
}

bool MseRequest::IsMseRequest(const std::string &url) {
    return url.find(MSE_PATH) != std::string::npos;
}

bool MseRequest::Open(CefRefPtr<CefRequest> request, bool &handle_request, CefRefPtr<CefCallback> callback) {
    std::string url = request->GetURL().ToString();
    std::string path = url.substr(url.find(MSE_PATH) + strlen(MSE_PATH));

    std::string op = path.substr(0, path.find_first_of("/?"));
    int track = -1;
    if (op == "append") {
        try {
            track = std::stoi(path.substr(strlen("append/")));
        } catch (...) {
            track = -1;
        }
    }

    std::string data;
    CefRefPtr<CefPostData> postData = request->GetPostData();
    if (postData != nullptr) {
        CefPostData::ElementVector elements;
        postData->GetElements(elements);

        for (const auto &e : elements) {
            if (e->GetType() == PDE_TYPE_BYTES) {
                size_t count = e->GetBytesCount();
                size_t offset = data.size();
                data.resize(offset + count);
                e->GetBytes(count, data.data() + offset);
            }
        }
    }

    // the thrift call shall not block the IO thread
    CefRefPtr<MseRequest> self(this);
    std::thread([self, callback, op, track, data = std::move(data)]() {
        self->status = self->process(op, track, data) ? 200 : (op == "append" ? 507 : 500);
        callback->Continue();
    }).detach();

    handle_request = false;
    return true;
}

bool MseRequest::process(const std::string &op, int track, const std::string &data) {
    std::lock_guard<std::mutex> lock(transcoderMutex);

    if (transcoderClient == nullptr) {
        transcoderClient = new TranscoderClient(bParam.transcoderIp, bParam.transcoderPort);
    }

    std::string streamId = bParam.browserIp + "_" + std::to_string(bParam.browserPort);

    if (op == "append" && track >= 0) {
        return transcoderClient->MseAppend(streamId, track, data);
    } else if (op == "reset") {
        return transcoderClient->MseReset(streamId);
    } else if (op == "end") {
        return transcoderClient->MseEndOfStream(streamId);
    }

    ERROR("MseRequest: unknown request {}", op);
    return false;
}

bool MseRequest::Read(void *data_out, int bytes_to_read, int &bytes_read, CefRefPtr<CefResourceReadCallback> callback) {
    bytes_read = 0;
    return false;
}

void MseRequest::GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &response_length, CefString &redirectUrl) {
    int s = status;
    response->SetStatus(s);
    response->SetStatusText(s == 200 ? "OK" : s == 507 ? "Insufficient Storage" : "Internal Server Error");
    response->SetMimeType("text/plain");

    CefResponse::HeaderMap header;
    header.insert(std::make_pair("Cache-Control", "no-store"));
    response->SetHeaderMap(header);

    response_length = 0;
}
