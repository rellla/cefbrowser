#pragma once

#include <string>
#include "thrift-services/src-client/TranscoderClient.h"
#include "cef_includes.h"
#include "logger.h"

class MovieStream : public CefResourceRequestHandler,
                    public CefResourceHandler {
public:
    MovieStream(TranscoderClient *client);

    // CefResourceRequestHandler
    CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                                     CefRefPtr<CefRequest> request) override {
        return this;
    }

    CefResourceRequestHandler::ReturnValue
    OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
                         CefRefPtr<CefCallback> callback) override;

    void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
                                CefRefPtr<CefResponse> response, URLRequestStatus status,
                                int64_t received_content_length) override;

    // CefResourceHandler
    bool Open(CefRefPtr<CefRequest> request, bool &handle_request, CefRefPtr<CefCallback> callback) override;

    bool Read(void *data_out, int bytes_to_read, int &bytes_read, CefRefPtr<CefResourceReadCallback> callback) override;

    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &response_length, CefString &redirectUrl) override;

    void Cancel() override;

private:
    size_t offset;
    size_t range_start;
    size_t range_end;      // inclusive
    bool is_range_request;
    size_t download_total;
    std::string download_data;
    TranscoderClient *transcoderClient;

private:
    IMPLEMENT_REFCOUNTING(MovieStream);
};