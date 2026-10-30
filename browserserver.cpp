#include <iostream>
#include <functional>
#include "browserserver.h"
#include "VdrClient.h"
#include "mainapp.h"
#include "logger.h"
#include "database.h"
#include "browserclient.h"
#include "keycodes.h"
#include "json.hpp"
#include "include/wrapper/cef_closure_task.h"
#include "include/base/cef_callback.h"

std::string lastInsertChannel = "";
std::string streamId;

// The thrift server runs independently of the browser: requests may arrive before the
// browser is created or after it has been closed. Keep a reference for the request.
#define GET_BROWSER(browser, result) \
    CefRefPtr<CefBrowser> browser = currentBrowser; \
    if (!browser) { \
        ERROR("{}: browser not available", __FUNCTION__); \
        return result; \
    }
// Most CefBrowser/CefFrame/CefBrowserHost methods must be called on the UI thread
// (e.g. ExecuteDevToolsMethod), the thrift requests arrive on their own threads.
// The tasks are executed in the order they are posted.
static void runOnUiThread(std::function<void()> task) {
    if (CefCurrentlyOn(TID_UI)) {
        task();
    } else {
        CefPostTask(TID_UI, base::BindOnce([](std::function<void()> t) { t(); }, std::move(task)));
    }
}

static void loadUrlOnUi(CefRefPtr<CefBrowser> browser, const std::string& url) {
    runOnUiThread([browser, url]() {
        auto frame = browser->GetMainFrame();
        if (frame != nullptr) {
            frame->LoadURL(url);
        } else {
            ERROR("MainFrame is null");
        }
    });
}

static void executeJavaScriptOnUi(CefRefPtr<CefBrowser> browser, const std::string& script) {
    runOnUiThread([browser, script]() {
        auto frame = browser->GetMainFrame();
        if (frame != nullptr) {
            frame->ExecuteJavaScript(script, frame->GetURL(), 0);
        } else {
            ERROR("MainFrame is null");
        }
    });
}

static void changeUserAgentOnUi(CefRefPtr<CefBrowser> browser, const std::string& agent) {
    runOnUiThread([browser, agent]() {
        CefRefPtr<CefClient> client = browser->GetHost()->GetClient();
        auto c = dynamic_cast<BrowserClient *>(client.get());
        if (c != nullptr) {
            c->ChangeUserAgent(browser, agent);
        }
    });
}

BrowserParameter BrowserServer::bParameterServer;

BrowserServer::~BrowserServer() {
};

void BrowserServer::setBrowserParameter(BrowserParameter bp) {
    bParameterServer = bp;
    streamId = bParameterServer.browserIp + "_" + std::to_string(bParameterServer.browserPort);
};

void BrowserServer::ping() {
}

bool BrowserServer::LoadUrl(const LoadUrlType &input) {
    GET_BROWSER(browser, false)

    INFO("Load URL: {}", input.url);

    if (input.url == BLANK_PAGE) {
        // special case
        StopVideoType stopType;
        stopType.reason = input.url;
        StopVideo(stopType);
    }

    loadUrlOnUi(browser, input.url);

    return true;
}

bool BrowserServer::RedButton(const RedButtonType &input) {
    GET_BROWSER(browser, false)

    INFO("Red Button for channelId: {}", input.channelId);

    std::string url;

    // iterate multiple times.
    for (int i = 0; i < 5; ++i) {
        url = database->getRedButtonUrl(input.channelId);
        if (!url.empty()) {
            break;
        }

        DEBUG("RedButton currently not available. Wait 250ms for a retry.");
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    if (url.empty()) {
        INFO("RedButton for channelId '{}' finally not found. Sent 404.", input.channelId);
        return false;
    } else {
        DEBUG("RedButton URL found: {}", url);

        // write channel information
        std::string channel = database->getChannel(input.channelId);
        std::ofstream _dynamic;
        _dynamic.open (bParameterServer.static_path + "/js/_dynamic.js", std::ios_base::trunc);
        _dynamic << "window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {}; window.HBBTV_POLYFILL_NS.currentChannel = " << channel << std::endl;
        _dynamic.close();

        CefRefPtr<CefClient> currentClient = browser->GetHost()->GetClient();
        auto c = dynamic_cast<BrowserClient *>(currentClient.get());
        if (c != nullptr) {
            c->enableProcessing(true);
        }

        std::string newUserAgent = database->getUserAgent(input.channelId);
        INFO("Use UserAgent {} for {}", newUserAgent, input.channelId);
        changeUserAgentOnUi(browser, newUserAgent);

        // load url
        loadUrlOnUi(browser, url);
    }

    return true;
}

bool BrowserServer::ReloadOSD() {
    GET_BROWSER(browser, false)

    runOnUiThread([browser]() { browser->GetHost()->Invalidate(PET_VIEW); });
    return true;
}

bool BrowserServer::StartApplication(const StartApplicationType &input) {
    GET_BROWSER(browser, false)

    INFO("Start Application, channelId {}, appId {}, url {}", input.channelId, input.appId, input.url);

    std::string channel = database->getChannel(input.channelId);
    std::ofstream _dynamic;
    _dynamic.open (bParameterServer.static_path + "/js/_dynamic.js", std::ios_base::trunc);

    // write channel information and parameters
    _dynamic << "window.HBBTV_POLYFILL_NS = window.HBBTV_POLYFILL_NS || {}; window.HBBTV_POLYFILL_NS.currentChannel = " << channel << std::endl;

    if (input.url.empty()) {
        _dynamic << "window.HBBTV_POLYFILL_NS.paramBody = '';" << std::endl;
    } else if (input.appId == "M3U") {
        // the m3u catalogue is a json object
        _dynamic << "window.HBBTV_POLYFILL_NS.paramBody = " << input.url << ";" << std::endl;
    } else {
        // an url must be a string literal, otherwise the whole script is invalid
        // and currentChannel is not set either
        _dynamic << "window.HBBTV_POLYFILL_NS.paramBody = " << nlohmann::json(input.url).dump() << ";" << std::endl;
    }

    _dynamic.close();

    CefRefPtr<CefClient> currentClient = browser->GetHost()->GetClient();
    auto c = dynamic_cast<BrowserClient *>(currentClient.get());
    if (c == nullptr) {
        return false;
    }

    changeUserAgentOnUi(browser, input.appUserAgent);

    std::string url;

    // create application url
    if (input.appId == "MAIN") {
        url = "http://" + bParameterServer.browserIp + ":" + std::to_string(bParameterServer.browserPort) + "/application/main/main.html";
        c->enableProcessing(true);
    } else if (input.appId == "URL") {
        url = input.url;
        c->enableProcessing(false);
    } else if (input.appId == "M3U") {
        url = "http://" + bParameterServer.browserIp + ":" + std::to_string(bParameterServer.browserPort) + "/application/iptv/index.html";
        c->enableProcessing(true);
    }

    DEBUG("Load URL: {}", url);

    // sanity check
    if (url.empty()) {
        url = BLANK_PAGE;
    }

    // load url
    loadUrlOnUi(browser, url);

    return true;
}

bool BrowserServer::ProcessKey(const ProcessKeyType &input) {
    GET_BROWSER(browser, false)

    // HbbTV: the application gets only the keys of its keyset. Returning false
    // tells VDR to handle the key itself (e.g. the numeric keys switch the channel).
    if ((keySetGroup(input.key) & currentKeySet) == 0) {
        DEBUG("Key {} is not in the keyset 0x{:x} of the application", input.key, currentKeySet.load());
        return false;
    }

    if ((input.key.find("VK_BACK") != std::string::npos) ||
        (input.key.find("VK_RED") != std::string::npos) ||
        (input.key.find("VK_GREEN") != std::string::npos) ||
        (input.key.find("VK_YELLOW") != std::string::npos) ||
        (input.key.find("VK_BLUE") != std::string::npos) ||
        (input.key.find("VK_PLAY") != std::string::npos) ||
        (input.key.find("VK_PAUSE") != std::string::npos) ||
        (input.key.find("VK_STOP") != std::string::npos) ||
        (input.key.find("VK_FAST_FWD") != std::string::npos) ||
        (input.key.find("VK_REWIND") != std::string::npos)) {
        // send key event via Javascript
        std::ostringstream stringStream;
        stringStream << "window.cefKeyPress('" << input.key << "');";
        executeJavaScriptOnUi(browser, stringStream.str());
    } else {
        // send key event via code
        auto code = keyCodes.find(input.key);
        if (code == keyCodes.end()) {
            ERROR("Unknown key {}", input.key);
            return false;
        }

        int windowsKeyCode = code->second;
        runOnUiThread([browser, windowsKeyCode]() {
            CefKeyEvent keyEvent;

            keyEvent.windows_key_code = windowsKeyCode;
            keyEvent.modifiers = 0x00;

            keyEvent.type = KEYEVENT_RAWKEYDOWN;
            browser->GetHost()->SendKeyEvent(keyEvent);

            keyEvent.type = KEYEVENT_CHAR;
            browser->GetHost()->SendKeyEvent(keyEvent);

            keyEvent.type = KEYEVENT_KEYUP;
            browser->GetHost()->SendKeyEvent(keyEvent);
        });
    }

    return true;
}

bool BrowserServer::StreamError(const StreamErrorType &input) {
    GET_BROWSER(browser, false)

    ERROR("Transcoder returned a stream error: {}", input.reason);

    executeJavaScriptOnUi(browser, "if (document.getElementsByTagName('video')[0]) { document.getElementsByTagName('video')[0].currentTime = document.getElementsByTagName('video')[0].duration; }");
    executeJavaScriptOnUi(browser, "window.cefKeyPress('VK_BACK');");

    StopVideoType stopType;
    stopType.reason = "stream error";
    StopVideo(stopType);

    return true;
}

bool BrowserServer::InsertHbbtv(const InsertHbbtvType &input) {
    // TRACE("InsertHbbtv: {}", body);
    return database->insertHbbtv(input.hbbtv);
}

bool BrowserServer::InsertChannel(const InsertChannelType &input) {
    // TRACE("InsertChannel: {}", body);

    if (input.channel != lastInsertChannel) {
        // in case of channel switch always stop playing videos
        std::string reason = "ChannelSwitch";

        StopVideoType stopType;
        stopType.reason = "ChannelSwitch";
        StopVideo(stopType);

        lastInsertChannel = input.channel;
    }

    return database->insertChannel(input.channel);
}

bool BrowserServer::StopVideo(const StopVideoType &input) {
    GET_BROWSER(browser, false)

    CefRefPtr<CefClient> currentClient = browser->GetHost()->GetClient();
    auto c = dynamic_cast<BrowserClient *>(currentClient.get());
    if (c != nullptr) {
        c->getCurrentTranscoderClient()->Stop(streamId, input.reason);
    }

    return true;
}

CefBrowserIf *BrowserCloneFactory::getHandler(const TConnectionInfo &connInfo) {
    std::shared_ptr<TSocket> sock = std::dynamic_pointer_cast<TSocket>(connInfo.transport);

    /*
    std::cout << "Incoming connection\n";
    std::cout << "\tSocketInfo: "  << sock->getSocketInfo() << "\n";
    std::cout << "\tPeerHost: "    << sock->getPeerHost() << "\n";
    std::cout << "\tPeerAddress: " << sock->getPeerAddress() << "\n";
    std::cout << "\tPeerPort: "    << sock->getPeerPort() << "\n";
    */

    return new BrowserServer();

}

void BrowserCloneFactory::releaseHandler(CommonServiceIf *handler) {
    delete handler;
}
