#include <chrono>
#include <thread>
#include <atomic>

#include "v8handler.h"
#include "logger.h"
#include "browserclient.h"

// used by the javascript thread and the stop thread
std::atomic<bool> startVideo(false);
std::atomic<bool> videoReset(false);
std::atomic<bool> stopVideoThreadRunning(false);

std::string videoInfo;
std::string oldVideoInfo;

void V8Handler::stopVdrVideo() {
    int waitTime = 10; // ms
    int count = 5000 / waitTime;
    startVideo = false;

    while (count-- > 0 && !startVideo) {
        std::this_thread::sleep_for(std::chrono::milliseconds(waitTime));
    }

    if (startVideo) {
        DEBUG("Thread stopVdrVideo, resetVideo");

        vdrClient->ResetVideo(videoInfo);
        videoReset = true;
    } else {
        DEBUG("Thread stopVdrVideo, stopVideo");

        videoInfo = "";
        vdrClient->StopVideo();
        videoReset = false;
    }

    stopVideoThreadRunning = false;
}

V8Handler::V8Handler(BrowserParameter bParam) : bParam(bParam)
{
    DEBUG("Create new V8Handler");
    transcoderClient = new TranscoderClient(bParam.transcoderIp, bParam.transcoderPort);
    vdrClient = new VdrClient(bParam.vdrIp, bParam.vdrPort);

    videoInfo = "";
    videoReset = false;
    stopVideoThreadRunning = false;
    isVideoPaused = false;
}

V8Handler::~V8Handler() {
    delete transcoderClient;
    delete vdrClient;
}

void V8Handler::sendMessageToProcess(std::string message) {
    sendMessageToProcess(message, "");
}

void V8Handler::sendMessageToProcess(std::string message, std::string parameter) {
    CefRefPtr<CefV8Context> ctx = CefV8Context::GetCurrentContext();
    CefRefPtr<CefBrowser> browser = ctx->GetBrowser();

    CefRefPtr<CefProcessMessage> msg= CefProcessMessage::Create(message);
    CefRefPtr<CefListValue> args = msg->GetArgumentList();
    if (!parameter.empty()) {
        args->SetString(0, parameter);
    }

    browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

void V8Handler::sendMessageToProcess(std::string message, std::vector<std::string>& parameter) {
    CefRefPtr<CefV8Context> ctx = CefV8Context::GetCurrentContext();
    CefRefPtr<CefBrowser> browser = ctx->GetBrowser();

    CefRefPtr<CefProcessMessage> msg= CefProcessMessage::Create(message);
    CefRefPtr<CefListValue> args = msg->GetArgumentList();

    int idx = 0;
    for (const auto &item: parameter) {
        args->SetString(idx, item);
        idx++;
    }

    browser->GetMainFrame()->SendProcessMessage(PID_BROWSER, msg);
}

bool V8Handler::Execute(const CefString &name, CefRefPtr<CefV8Value> object, const CefV8ValueList &arguments, CefRefPtr<CefV8Value> &retval, CefString &exception) {
    DEBUG("V8Handler::Execute: {}", name.ToString());
    time_t now = time(nullptr);

    std::string streamId = bParam.browserIp + "_" + std::to_string(bParam.browserPort);

    if (name == "StreamVideo") {
        startVideo = true;

        if (!arguments.empty()) {
            const auto& urlParam = arguments.at(0);
            auto url = urlParam.get()->GetStringValue();

            const auto& cookiesParam = arguments.at(1);
            auto cookies = cookiesParam.get()->GetStringValue();

            const auto& refererParam = arguments.at(2);
            auto referer = refererParam.get()->GetStringValue();

            const auto& userAgentParam = arguments.at(3);
            auto userAgent = userAgentParam.get()->GetStringValue();

            const auto& mpdStartParam = arguments.at(4);
            auto mpdStart = mpdStartParam.get()->GetIntValue();

            // if video is pause, then some special handling is necessary
            if (isVideoPaused) {
                std::string reason = "Javascript StopVideo (Start after Pause)";
                transcoderClient->Stop(streamId, reason);
                videoReset = true;

                // the VDR player is still paused and would drop all packets of the new video
                vdrClient->Resume();
                isVideoPaused = false;
            }

            TRACE("Video URL: {}", url.ToString());

            // 1. Step call Probe
            // videoInfo is not touched by a failed request, don't keep the old value
            oldVideoInfo = videoInfo;
            videoInfo.clear();
            if (!transcoderClient->Probe(videoInfo, url, cookies, referer, userAgent, bParam.browserIp, std::to_string(bParam.browserPort), bParam.vdrIp, std::to_string(bParam.vdrPort), std::to_string(now))) {
                videoInfo.clear();
            }

            if (videoInfo.empty()) {
                ERROR("Probe failed: Wrong video URL, a server error or another error.");
                retval = CefV8Value::CreateNull(); // javascript sets the error state
                return true;
            }

            TRACE("Video Info:  {} -> {}", oldVideoInfo, videoInfo);
            if (!oldVideoInfo.empty() && oldVideoInfo != videoInfo) {
                INFO("--> Video format change");
            } else {
                INFO("--> Video format equal");
            }

            if (videoInfo.empty()) {
                // transcoder not available
                ERROR("Probe failed: Transcoder not available, wrong video URL or another error.");
                retval = CefV8Value::CreateNull(); // javascript sets the error state
                return true;
            }

            // 2. Step call StreamUrl
            if (!transcoderClient->StreamUrl(url, cookies, referer, userAgent, bParam.browserIp, std::to_string(bParam.browserPort), bParam.vdrIp, std::to_string(bParam.vdrPort), std::to_string(mpdStart))) {
                // transcoder not available
                ERROR("Unable to send request to transcoder");
                retval = CefV8Value::CreateNull(); // javascript sets the error state
                return true;
            }

            while (stopVideoThreadRunning) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }

            TRACE("VideoReset: {}", videoReset.load());

            if (!videoReset) {
                vdrClient->StartVideo(videoInfo);
            }

            // the reset applies to this video only. The next video without a
            // stop in between needs its own StartVideo.
            videoReset = false;
        }

        std::string newVideoUrl = "http://localhost/movie/transparent-video-" + bParam.browserIp + "_" + std::to_string(bParam.browserPort) + "-" + std::to_string(now) + ".webm";

        TRACE("Javascript return new Video URL: {}", newVideoUrl);

        retval = CefV8Value::CreateString(newVideoUrl);
        return true;
    } else if (name == "StopVideo") {
        isVideoPaused = false;

        DEBUG("V8Handler::Execute: in {}", name.ToString());
        std::string reason = "Javascript StopVideo";
        transcoderClient->Stop(streamId, reason);

        DEBUG("V8Handler::Execute: Trancoder finished {}", name.ToString());

        stopVideoThreadRunning = true;
        std::thread stopThread(&V8Handler::stopVdrVideo, this);
        stopThread.detach();

        DEBUG("V8Handler::Execute: Thread started {}", name.ToString());

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "PauseVideo") {
        isVideoPaused = true;

        vdrClient->Pause();
        transcoderClient->Pause(streamId);

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "ResumeVideo") {
        isVideoPaused = false;

        const auto &param = arguments.at(0);
        auto position = param.get()->GetStringValue().ToString();

        DEBUG("RESUME at timestamp {}", position);

        vdrClient->Resume();
        transcoderClient->Resume(streamId, position);

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "UnfreezeDevice") {
        isVideoPaused = false;
        DEBUG("UnfreezeDevice");

        vdrClient->Resume();
        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "SeekVideo") {
        isVideoPaused = false;

        if (!arguments.empty()) {
            const auto& param = arguments.at(0);
            auto position = param.get()->GetStringValue().ToString();

            DEBUG("V8Handler::Execute SeekVideo Argument Pos {}", position);

            transcoderClient->SeekTo(streamId, position);
            vdrClient->ResetVideo(videoInfo);
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "RedButton") {
        if (!arguments.empty()) {
            const auto& param = arguments.at(0);
            auto channelId = param.get()->GetStringValue().ToString();

            DEBUG("V8Handler::Execute RedButton Argument ChannelId {}", channelId);

            sendMessageToProcess("RedButton", channelId);
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "LoadUrl") {
        if (!arguments.empty()) {
            const auto& param = arguments.at(0);
            auto url = param.get()->GetStringValue().ToString();

            DEBUG("V8Handler::Execute LoadUrl {}", url);

            sendMessageToProcess("LoadUrl", url);
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    }  else if (name == "VideoSize") {
        if (!arguments.empty()) {
            const auto x = arguments.at(0)->GetIntValue();
            const auto y = arguments.at(1)->GetIntValue();
            const auto w = arguments.at(2)->GetIntValue();
            const auto h = arguments.at(3)->GetIntValue();

            DEBUG("VideoSize New({}, {}, {}, {}", x,y,w,h);
            vdrClient->VideoSize(x, y, w, h);
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "VideoFullscreen") {
        DEBUG("Set VideoFullScreen");
        vdrClient->VideoFullscreen();

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "StartApp") {
        if (!arguments.empty()) {
            const auto channelId = arguments.at(0)->GetStringValue();
            const auto appId = arguments.at(1)->GetStringValue();
            const auto args = arguments.at(2)->GetStringValue();

            std::vector<std::string> params;
            params.push_back(channelId);
            params.push_back(appId);
            params.push_back(args);

            sendMessageToProcess("StartApp", params);
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "AudioInfo") {
        std::string result;
        transcoderClient->AudioInfo(result, streamId);
        retval = CefV8Value::CreateString(result);
        return true;
    } else if (name == "SelectAudioTrack") {
        if (!arguments.empty()) {
            auto nr = std::to_string(arguments.at(0)->GetIntValue());
            bool result = vdrClient->SelectAudioTrack(nr);
            retval = CefV8Value::CreateBool(result);
            return true;
        }
        retval = CefV8Value::CreateBool(true);
        return true;
    } else {
        ERROR("Javascript function {} is not implemented.", name.ToString());
    }

    // Function does not exist.
    return false;
}
