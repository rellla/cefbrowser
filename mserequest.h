#pragma once

#include <string>
#include <atomic>
#include "cef_includes.h"
#include "tools.h"

// Media Source Extensions: the segments appended by the player of the page.
//
// The javascript MediaSource sends the data with fetch() to the origin of the page
// (no CORS, no mixed content), the request never leaves the browser:
//   POST <origin>/__vdrmse__/append/<SourceBuffer index>   body: the segment
//   POST <origin>/__vdrmse__/reset                         a seek
//   POST <origin>/__vdrmse__/end                           end of stream
// The answer is 200, or 507 if the transcoder has no space left.
class MseRequest : public CefResourceRequestHandler,
                   public CefResourceHandler {
public:
    explicit MseRequest(const BrowserParameter &bParam);

    static bool IsMseRequest(const std::string &url);

    // CefResourceRequestHandler
    CefRefPtr<CefResourceHandler> GetResourceHandler(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                                     CefRefPtr<CefRequest> request) override {
        return this;
    }

    // CefResourceHandler
    bool Open(CefRefPtr<CefRequest> request, bool &handle_request, CefRefPtr<CefCallback> callback) override;
    bool Read(void *data_out, int bytes_to_read, int &bytes_read, CefRefPtr<CefResourceReadCallback> callback) override;
    void GetResponseHeaders(CefRefPtr<CefResponse> response, int64_t &response_length, CefString &redirectUrl) override;
    void Cancel() override {}

private:
    bool process(const std::string &op, int track, const std::string &data);

    BrowserParameter bParam;
    std::atomic<int> status;

private:
    IMPLEMENT_REFCOUNTING(MseRequest);
};
