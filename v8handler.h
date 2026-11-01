#pragma once

#include "cef_includes.h"

#include "thrift-services/src-client/TranscoderClient.h"
#include "thrift-services/src-client/VdrClient.h"
#include "tools.h"

class V8Handler : public CefV8Handler {
public:
    V8Handler(BrowserParameter bParam);
    ~V8Handler();

    bool Execute(const CefString& name, CefRefPtr<CefV8Value> object, const CefV8ValueList& arguments, CefRefPtr<CefV8Value>& retval, CefString& exception) override;

private:
    BrowserParameter bParam;

    VdrClient *vdrClient;
    TranscoderClient *transcoderClient;

    bool isVideoPaused;

private:
    void sendMessageToProcess(std::string message);
    void sendMessageToProcess(std::string message, std::string parameter);
    void sendMessageToProcess(std::string message, std::vector<std::string>& parameter);

    void stopVdrVideo(uint64_t generation);
    void startVdrVideo(bool mse, bool playerReset);

    IMPLEMENT_REFCOUNTING(V8Handler);
};
