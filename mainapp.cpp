#include "mainapp.h"

#include "browserserver.h"
#include "CefBrowser.h"
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

#include <utility>
#include "browserclient.h"
#include "v8handler.h"
#include "tools.h"
#include "keycodes.h"
#include "database.h"

using namespace ::apache::thrift;
using namespace ::apache::thrift::protocol;
using namespace ::apache::thrift::transport;
using namespace ::apache::thrift::server;
using namespace ::apache::thrift::concurrency;
using namespace ::common;

scoped_refptr<CefBrowser> currentBrowser;
TThreadedServer *thriftServer;

void startServer(BrowserParameter bParam) {
    BrowserServer::setBrowserParameter(bParam);

    std::string listenIp = bParam.bindAll ? "0.0.0.0" : bParam.browserIp;

    // one thread per connection: the clients keep their connections open,
    // a fixed pool of 4 workers blocks any further client
    thriftServer = new TThreadedServer(
            std::make_shared<CefBrowserProcessorFactory>(std::make_shared<BrowserCloneFactory>()),
            std::make_shared<TServerSocket>(listenIp, bParam.browserPort),
            std::make_shared<TBufferedTransportFactory>(),
            std::make_shared<TBinaryProtocolFactory>());

    thriftServer->serve();
}

bool stopCheckVdrRegular = false;

void checkVdrRegular() {
    while (!stopCheckVdrRegular) {
        if (currentBrowser != nullptr) {
            std::string url = currentBrowser->GetMainFrame()->GetURL().ToString();
            if (!url.empty() && url != BLANK_PAGE) {
                auto c = dynamic_cast<BrowserClient *>(currentBrowser->GetHost()->GetClient().get());
                if (!c->IsVdrWebActive()) {
                    currentBrowser->GetMainFrame()->LoadURL(BLANK_PAGE);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// BrowserApp
BrowserApp::BrowserApp(BrowserParameter bparam) : bParameter(bparam) {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
}

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler() {
    return this;
}

CefRefPtr<CefRenderProcessHandler> BrowserApp::GetRenderProcessHandler() {
    return this;
}

void BrowserApp::OnBeforeCommandLineProcessing(const CefString& process_type, CefRefPtr<CefCommandLine> command_line) {
    command_line->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");

    // Offscreen rendering only needs software compositing, the OSD is drawn by VDR.
    // Without these switches Chromium tries GLES 3.0 (the RPi4 offers 2.0), the GPU
    // process fails and is restarted in software mode. Run the remaining GPU thread
    // inside the browser process, this saves a process and its memory.
    if (process_type.empty()) {
        command_line->AppendSwitch("disable-gpu");
        command_line->AppendSwitch("disable-gpu-compositing");
        command_line->AppendSwitch("in-process-gpu");
    }
}

void BrowserApp::OnContextInitialized() {
    LOG_CURRENT_THREAD();

    DEBUG("BrowserApp::OnContextInitialized()");

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 25;

    CefWindowInfo window_info;
    window_info.SetAsWindowless(0);
    window_info.shared_texture_enabled = false;

    CefRefPtr<CefDictionaryValue> extra_info = CefDictionaryValue::Create();
    extra_info->SetString("browserIp", bParameter.browserIp);
    extra_info->SetInt("browserPort", bParameter.browserPort);
    extra_info->SetString("transcoderIp", bParameter.transcoderIp);
    extra_info->SetInt("transcoderPort", bParameter.transcoderPort);
    extra_info->SetString("vdrIp", bParameter.vdrIp);
    extra_info->SetInt("vdrPort", bParameter.vdrPort);
    extra_info->SetInt("LogLevel", logger->level());
    extra_info->SetInt("osdqoi", (int)bParameter.osdqoi);
    extra_info->SetBool("dirtyRecs", bParameter.use_dirty_recs);

    CefRefPtr<BrowserClient> client = new BrowserClient(false, bParameter);
    currentBrowser = CefBrowserHost::CreateBrowserSync(window_info, client, "", browserSettings, extra_info, nullptr);

    // read user agents if configured
    if (!bParameter.user_agent_path.empty()) {
        database->readUserAgents(bParameter.user_agent_path);
    }

    if (logger->isTraceEnabled()) {
        auto pref_manager = currentBrowser->GetHost()->GetRequestContext();

        // print original values
        if (logger->isTraceEnabled()) {
            auto prefs = pref_manager->GetAllPreferences(true);

            CefRefPtr<CefValue> prefValue = CefValue::Create();
            prefValue->SetDictionary(prefs);
            auto cc = CefWriteJSON(prefValue, JSON_WRITER_DEFAULT);
            TRACE("Browser Preferences JSON:\n{}", cc.ToString());
        }

        // profile.content_settings.mixed_script
        //    [*.]ard.de,*
        //       last_modified: <unix time stamp>
        //       setting: 1

        // last_modified: <unix time stamp>
        auto lastModified = CefValue::Create();
        lastModified->SetInt(112233);

        // setting: 1
        auto setting = CefValue::Create();
        setting->SetInt(1);

        //       last_modified: <unix time stamp>
        //       setting: 1
        auto subDictValue = CefDictionaryValue::Create();
        subDictValue->SetValue("last_modified", lastModified);
        subDictValue->SetValue("setting", setting);

        // profile.content_settings.exceptions.mixed_script
        auto dictValue = CefDictionaryValue::Create();
        dictValue->SetDictionary("[*.]ard.de,*", subDictValue);

        auto value = CefValue::Create();
        value->SetDictionary(dictValue);

        CefString msg;
        bool success = pref_manager->SetPreference("profile.content_settings.exceptions.mixed_script", value, msg);
        TRACE("Result setting value {} -> {}", success, msg.ToString());

        // print values
        if (logger->isTraceEnabled()) {
            CefRefPtr<CefValue> avalue = CefValue::Create();
            auto aprefs = pref_manager->GetAllPreferences(false);
            avalue->SetDictionary(aprefs);
            auto acc = CefWriteJSON(avalue, JSON_WRITER_DEFAULT);
            TRACE("Neu Browser Preferences JSON:\n{}", acc.ToString());
        }
    }

    INFO("Start Server on {}:{} with static path {}", bParameter.browserIp, bParameter.browserPort, bParameter.static_path);

    std::thread t1(startServer, bParameter);
    t1.detach();

    std::thread t2(checkVdrRegular);
    t2.detach();
}

void BrowserApp::OnWebKitInitialized() {
}

void BrowserApp::OnBrowserCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefDictionaryValue> extra_info) {
    bParameter.browserIp = extra_info->GetString("browserIp");
    bParameter.browserPort = extra_info->GetInt("browserPort");
    bParameter.transcoderIp = extra_info->GetString("transcoderIp");
    bParameter.transcoderPort = extra_info->GetInt("transcoderPort");
    bParameter.vdrIp = extra_info->GetString("vdrIp");
    bParameter.vdrPort = extra_info->GetInt("vdrPort");
    bParameter.osdqoi = (image_type_enum)extra_info->GetInt("osdqoi");
    bParameter.use_dirty_recs = extra_info->GetBool("dirtyRecs");

    int logLevel = extra_info->GetInt("LogLevel");
    logger->set_level(static_cast<spdlog::level::level_enum>(logLevel));
}

void BrowserApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
    TRACE("BrowserApp::OnContextCreated");

    // register all native JS functions
    CefRefPtr<CefV8Value> object = context->GetGlobal();
    handler = new V8Handler(bParameter);

    CefRefPtr<CefV8Value> streamVideo = CefV8Value::CreateFunction("StreamVideo", handler);
    object->SetValue("cefStreamVideo", streamVideo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> stopVideo = CefV8Value::CreateFunction("StopVideo", handler);
    object->SetValue("cefStopVideo", stopVideo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> pauseVideo = CefV8Value::CreateFunction("PauseVideo", handler);
    object->SetValue("cefPauseVideo", pauseVideo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> resumeVideo = CefV8Value::CreateFunction("ResumeVideo", handler);
    object->SetValue("cefResumeVideo", resumeVideo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> unfreezeDevice = CefV8Value::CreateFunction("UnfreezeDevice", handler);
    object->SetValue("cefUnfreezeDevice", unfreezeDevice, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> seekVideo = CefV8Value::CreateFunction("SeekVideo", handler);
    object->SetValue("cefSeekVideo", seekVideo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> redButton = CefV8Value::CreateFunction("RedButton", handler);
    object->SetValue("cefRedButton", redButton, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> setKeySet = CefV8Value::CreateFunction("SetKeySet", handler);
    object->SetValue("cefSetKeySet", setKeySet, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> destroyApp = CefV8Value::CreateFunction("DestroyApp", handler);
    object->SetValue("cefDestroyApp", destroyApp, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> loadUrl = CefV8Value::CreateFunction("LoadUrl", handler);
    object->SetValue("cefLoadUrl", loadUrl, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> videoSize = CefV8Value::CreateFunction("VideoSize", handler);
    object->SetValue("cefVideoSize", videoSize, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> videoFull = CefV8Value::CreateFunction("VideoFullscreen", handler);
    object->SetValue("cefVideoFullscreen", videoFull, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> startApp = CefV8Value::CreateFunction("StartApp", handler);
    object->SetValue("cefStartApp", startApp, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> audioInfo = CefV8Value::CreateFunction("AudioInfo", handler);
    object->SetValue("cefAudioInfo", audioInfo, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> audioTrack = CefV8Value::CreateFunction("SelectAudioTrack", handler);
    object->SetValue("cefSelectAudioTrack", audioTrack, V8_PROPERTY_ATTRIBUTE_NONE);

    CefRefPtr<CefV8Value> subtitleTrack = CefV8Value::CreateFunction("SelectSubtitleTrack", handler);
    object->SetValue("cefSelectSubtitleTrack", subtitleTrack, V8_PROPERTY_ATTRIBUTE_NONE);
}

void BrowserApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
    TRACE("BrowserApp::OnContextReleased");
    handler = nullptr;
}

bool BrowserApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId source_process, CefRefPtr<CefProcessMessage> message) {
    TRACE("BrowserApp::OnProcessMessageReceived(Renderer) {}", message->GetName().ToString());

    return false;
}

void BrowserApp::shutdown() {
    if (thriftServer) {
        thriftServer->stop();
    }

    stopCheckVdrRegular = true;

    currentBrowser->Release();
    currentBrowser = nullptr;
}

