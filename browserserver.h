#pragma once

#include "CefBrowser.h"
#include "tools.h"
#include "thrift-services/src-client/TranscoderClient.h"

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TBufferTransports.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TBufferTransports.h>

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;
using namespace ::cefbrowser;
using namespace ::common;

class BrowserServer : public CefBrowserIf {
private:
    static BrowserParameter bParameterServer;

public:
    ~BrowserServer() override;

    static void setBrowserParameter(BrowserParameter bp);

    void ping() override;

    bool LoadUrl(const LoadUrlType& input) override;
    bool RedButton(const RedButtonType& input) override;
    bool ReloadOSD() override;
    bool StartApplication(const StartApplicationType& input) override;
    bool ProcessKey(const ProcessKeyType& input) override;
    bool StreamError(const StreamErrorType& input) override;
    bool InsertHbbtv(const InsertHbbtvType& input) override;
    bool InsertChannel(const InsertChannelType& input) override;
    bool StopVideo(const StopVideoType& input) override;
};

class BrowserCloneFactory : virtual public CefBrowserIfFactory {
public:
    ~BrowserCloneFactory() override = default;

    CefBrowserIf* getHandler(const TConnectionInfo& connInfo) override;
    void releaseHandler(CommonServiceIf* handler) override;
};
