#pragma once

#include "cef_includes.h"
#include "logger.h"
#include "tools.h"
#include "cpr/connection_pool.h"
#include "thrift-services/src-client/TranscoderClient.h"
#include "thrift-services/src-client/VdrClient.h"
#include "osdsender.h"

class BrowserClient : public CefClient,
                      public CefRenderHandler,
                      public CefLifeSpanHandler,
                      public CefRequestHandler,
                      public CefDisplayHandler,
                      public CefFrameHandler {

public:
    explicit BrowserClient(bool fullscreen, BrowserParameter bParameter);

    ~BrowserClient() override;

    // RenderHandler
    CefRefPtr<CefRenderHandler> GetRenderHandler() override {
        return this;
    };

    // CefLifeSpanHandler
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    // CefRequestHandler
    CefRefPtr<CefRequestHandler> GetRequestHandler() override {
        return this;
    }

    // CefDisplayHandler
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override {
        // Disabled
        return nullptr;
    }

    // CefFrameHandler
    CefRefPtr<CefFrameHandler> GetFrameHandler() override {
        return this;
    }

    // CefClient
    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) override;

    // CefResourceRequestHandler
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
            CefRefPtr<CefBrowser> browser,
            CefRefPtr<CefFrame> frame,
            CefRefPtr<CefRequest> request,
            bool is_navigation,
            bool is_download,
            const CefString &request_initiator,
            bool &disable_default_handling) override;

    // RenderHandler
    void setRenderSize(int width, int height);

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect &rect) override;

    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type, const RectList &dirtyRects, const void *buffer,
                 int width, int height) override;

    // LifeSpanHandler
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr< CefBrowser > browser) override;

    // RequestHandler
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool user_gesture, bool is_redirect) override;
    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status, int error_code, const CefString& error_string) override;

    // CefDisplayHandler
    void OnStatusMessage(CefRefPtr<CefBrowser> browser, const CefString& value) override;
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser, cef_log_severity_t level, const CefString& message, const CefString& source, int line) override;

    // CefFrameHandler
    void OnFrameCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) override;
    void OnFrameAttached(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, bool reattached) override;
    void OnFrameDetached(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame) override;
    void OnMainFrameChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> old_frame, CefRefPtr<CefFrame> new_frame) override;

    // Change User Agent
    void ChangeUserAgent(CefRefPtr<CefBrowser> browser, std::string agent);

    // process the whole URL loading
    void enableProcessing(bool processUrl);

    TranscoderClient* getCurrentTranscoderClient() { return transcoderClient; };
    bool IsVdrWebActive() { return vdrClient != nullptr ? vdrClient->IsWebActive() : true; };

private:
    void loadUrl(CefRefPtr<CefBrowser> browser, const std::string& url);

private:
    int renderWidth;
    int renderHeight;
    bool fullscreen;
    std::string streamId;

    bool processorEnabled;

    BrowserParameter bParam;

    int videoX, videoY, videoW, videoH;
    bool videoIsFullscreen;

    VdrClient* vdrClient;
    OsdSender* osdSender;
    TranscoderClient *transcoderClient;

    CefRefPtr<CefRegistration> registration;

    cpr::ConnectionPool pool;

private:
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

class DevToolsMessageObserver : public CefDevToolsMessageObserver {

public:
    DevToolsMessageObserver() = default;

    bool OnDevToolsMessage(CefRefPtr<CefBrowser> browser, const void* message, size_t message_size) override;
    void OnDevToolsMethodResult(CefRefPtr<CefBrowser> browser, int message_id, bool success, const void* result, size_t result_size) override;
    void OnDevToolsEvent(CefRefPtr<CefBrowser> browser, const CefString& method, const void* params, size_t params_size) override;
    void OnDevToolsAgentAttached(CefRefPtr<CefBrowser> browser) override;
    void OnDevToolsAgentDetached(CefRefPtr<CefBrowser> browser) override;

private:
    IMPLEMENT_REFCOUNTING(DevToolsMessageObserver);
    DISALLOW_COPY_AND_ASSIGN(DevToolsMessageObserver);
};