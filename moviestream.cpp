#include <cstdio>
#include "tools.h"
#include "moviestream.h"
#include "json.hpp"
#include "TranscoderClient.h"

// CefResourceRequestHandler
CefResourceRequestHandler::ReturnValue MovieStream::OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) {
    TRACE("MovieStream::OnBeforeResourceLoad: {}", request->GetURL().ToString());

    return RV_CONTINUE;
}

MovieStream::MovieStream(TranscoderClient *client) : offset(0), range_start(0), range_end(0), is_range_request(false), download_total(0), transcoderClient(client) {
}

bool MovieStream::Open(CefRefPtr<CefRequest> request, bool& handle_request, CefRefPtr<CefCallback> callback) {
    TRACE("MovieStream::Open: {}", request->GetURL().ToString());

    VideoType input;

    // special case: overwrite the video url and get a fake movie
    if (request->GetURL().ToString().find("_muted_onl") != std::string::npos) {
        input.filename = "fake-video.webm";
    } else {
        input.filename = request->GetURL().ToString().c_str() + strlen("http://localhost/movie/");
    }

    download_data.clear();
    if (!transcoderClient->GetVideo(download_data, input)) {
        ERROR("MovieStream: video {} not available", input.filename);
        download_data.clear();
    }
    download_total = download_data.length();

    // The media player seeks with range requests ("bytes=start-[end]").
    // Answering always from byte 0 corrupts the data after a seek.
    is_range_request = false;
    range_start = 0;
    range_end = download_total > 0 ? download_total - 1 : 0;

    std::string range = request->GetHeaderByName("Range").ToString();
    unsigned long long start = 0, end = 0;
    if (download_total > 0 && range.rfind("bytes=", 0) == 0) {
        int n = sscanf(range.c_str() + 6, "%llu-%llu", &start, &end);
        if (n >= 1 && start < download_total) {
            is_range_request = true;
            range_start = start;
            if (n == 2 && end >= start && end < download_total) {
                range_end = end;
            }
        }
    }

    offset = range_start;

    // handled immediately, the callback must not be used in this case
    handle_request = true;
    return true;
}

bool MovieStream::Read(void* data_out, int bytes_to_read, int& bytes_read, CefRefPtr<CefResourceReadCallback> callback) {
    // TRACE("MovieStream::Read: {}, Size {}, Offset {}", bytes_to_read, download_data.length(), offset);

    size_t size = download_total > 0 ? range_end + 1 : 0;
    if (offset < size) {
        int transfer_size = static_cast<int>(std::min(static_cast<size_t>(bytes_to_read), size - offset));
        memcpy(data_out, download_data.c_str() + offset, transfer_size);
        offset += transfer_size;

        bytes_read = transfer_size;
        return true;
    }

    return false;
}

void MovieStream::GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &response_length, CefString &redirectUrl) {
    TRACE("MovieStream::GetResponseHeaders");

    CefResponse::HeaderMap responseHeader;

    if (download_total == 0) {
        response->SetStatus(404);
        response->SetStatusText("Not Found");
        response_length = 0;
        return;
    }

    size_t length = range_end - range_start + 1;

    if (is_range_request) {
        response->SetStatus(206);
        response->SetStatusText("Partial Content");
        responseHeader.insert(std::make_pair("Content-Range", "bytes " + std::to_string(range_start) + "-" + std::to_string(range_end) + "/" + std::to_string(download_total)));
    } else {
        response->SetStatus(200);
        response->SetStatusText("OK");
    }

    response->SetMimeType("video/webm");

    responseHeader.insert(std::make_pair("Content-Type", "video/webm"));
    responseHeader.insert(std::make_pair("Accept-Ranges", "bytes"));
    responseHeader.insert(std::make_pair("Content-Length", std::to_string(length)));
    responseHeader.insert(std::make_pair("Vary", "Origin, Access-Control-Request-Headers"));

    response->SetHeaderMap(responseHeader);

    response_length = static_cast<int64_t>(length);
}

void MovieStream::Cancel() {
}

void MovieStream::OnResourceLoadComplete(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                             CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response,
                                             CefResourceRequestHandler::URLRequestStatus status,
                                             int64_t received_content_length) {
    // TRACE("MovieStream::OnResourceLoadComplete: {} -> {}", (int)status, received_content_length);

    CefResourceRequestHandler::OnResourceLoadComplete(browser, frame, request, response, status, received_content_length);
}