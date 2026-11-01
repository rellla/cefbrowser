#include <chrono>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

#include "v8handler.h"
#include "logger.h"
#include "browserclient.h"

std::string videoInfo;
std::string oldVideoInfo;

// the current video is a MSE stream
std::atomic<bool> videoMse(false);

// A stopped video keeps the VDR player for 5 seconds. A new video in this time
// resets the player instead of starting a new one (no flicker between two videos).
static std::mutex videoMutex;
static std::condition_variable videoCondition;
static bool stopPending = false;        // a stop thread waits for a new video
static bool takenOver = false;          // a new video has taken the player
static uint64_t stopGeneration = 0;

void V8Handler::stopVdrVideo(uint64_t generation) {
    std::unique_lock<std::mutex> lock(videoMutex);

    videoCondition.wait_for(lock, std::chrono::seconds(5), [generation] {
        return takenOver || generation != stopGeneration;
    });

    // a later stop has its own thread
    if (generation != stopGeneration) {
        return;
    }

    if (!takenOver) {
        DEBUG("Thread stopVdrVideo, stopVideo");
        stopPending = false;

        // under the lock: a new video waits until VDR has stopped the player
        vdrClient->StopVideo();
    }

    takenOver = false;
}

void V8Handler::startVdrVideo(bool mse, bool playerReset) {
    std::lock_guard<std::mutex> lock(videoMutex);

    bool reset = playerReset;
    if (stopPending) {
        DEBUG("startVdrVideo: take over the player of the stopped video");
        stopPending = false;
        takenOver = true;
        reset = true;
        videoCondition.notify_all();
    }

    if (reset) {
        vdrClient->ResetVideo(videoInfo, mse);
    } else {
        vdrClient->StartVideo(videoInfo, mse);
    }
}

V8Handler::V8Handler(BrowserParameter bParam) : bParam(bParam)
{
    DEBUG("Create new V8Handler");
    transcoderClient = new TranscoderClient(bParam.transcoderIp, bParam.transcoderPort);
    vdrClient = new VdrClient(bParam.vdrIp, bParam.vdrPort);

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
        videoMse = false;

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
            bool playerReset = false;
            if (isVideoPaused) {
                std::string reason = "Javascript StopVideo (Start after Pause)";
                transcoderClient->Stop(streamId, reason);
                playerReset = true;

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

            startVdrVideo(false, playerReset);
        }

        std::string newVideoUrl = "http://localhost/movie/transparent-video-" + bParam.browserIp + "_" + std::to_string(bParam.browserPort) + "-" + std::to_string(now) + ".webm";

        TRACE("Javascript return new Video URL: {}", newVideoUrl);

        retval = CefV8Value::CreateString(newVideoUrl);
        return true;
    } else if (name == "MseStart") {
        // Media Source Extensions: the segments of the player follow by http requests
        videoMse = true;

        std::vector<std::string> tracks;
        std::string duration = "N/A";

        if (!arguments.empty() && arguments.at(0)->IsArray()) {
            for (int i = 0; i < arguments.at(0)->GetArrayLength(); ++i) {
                tracks.push_back(arguments.at(0)->GetValue(i)->GetStringValue().ToString());
            }
        }

        if (arguments.size() >= 2 && arguments.at(1)->IsString()) {
            duration = arguments.at(1)->GetStringValue().ToString();
        }

        bool playerReset = false;
        if (isVideoPaused) {
            std::string reason = "Javascript StopVideo (Start after Pause)";
            transcoderClient->Stop(streamId, reason);
            playerReset = true;

            // the VDR player is still paused and would drop all packets of the new video
            vdrClient->Resume();
            isVideoPaused = false;
        }

        oldVideoInfo = videoInfo;
        videoInfo.clear();
        if (tracks.empty() || !transcoderClient->MseStart(videoInfo, bParam.browserIp, std::to_string(bParam.browserPort), bParam.vdrIp, std::to_string(bParam.vdrPort), std::to_string(now), duration, tracks)) {
            videoInfo.clear();
        }

        if (videoInfo.empty()) {
            ERROR("MseStart failed: Transcoder not available or another error.");
            retval = CefV8Value::CreateNull();
            return true;
        }

        startVdrVideo(true, playerReset);

        std::string newVideoUrl = "http://localhost/movie/transparent-video-" + bParam.browserIp + "_" + std::to_string(bParam.browserPort) + "-" + std::to_string(now) + ".webm";
        retval = CefV8Value::CreateString(newVideoUrl);
        return true;
    } else if (name == "StopVideo") {
        isVideoPaused = false;

        DEBUG("V8Handler::Execute: in {}", name.ToString());
        std::string reason = "Javascript StopVideo";
        transcoderClient->Stop(streamId, reason);

        DEBUG("V8Handler::Execute: Trancoder finished {}", name.ToString());

        uint64_t generation;
        {
            std::lock_guard<std::mutex> lock(videoMutex);
            stopPending = true;
            takenOver = false;
            generation = ++stopGeneration;
        }
        videoCondition.notify_all();

        // the detached thread keeps a reference, the handler may be released meanwhile (page change)
        CefRefPtr<V8Handler> self(this);
        std::thread stopThread([self, generation]() { self->stopVdrVideo(generation); });
        stopThread.detach();

        DEBUG("V8Handler::Execute: Thread started {}", name.ToString());

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "PauseVideo") {
        isVideoPaused = true;

        if (videoMse) {
            // the transcoder stops sending first, VDR keeps the data of a MSE stream
            transcoderClient->Pause(streamId);
            vdrClient->Pause();
        } else {
            vdrClient->Pause();
            transcoderClient->Pause(streamId);
        }

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
            vdrClient->ResetVideo(videoInfo, videoMse);
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
    } else if (name == "SetKeySet") {
        if (!arguments.empty() && (arguments.at(0)->IsInt() || arguments.at(0)->IsUInt() || arguments.at(0)->IsDouble())) {
            int value = static_cast<int>(arguments.at(0)->GetDoubleValue());
            sendMessageToProcess("KeySet", std::to_string(value));
        }

        retval = CefV8Value::CreateBool(true);
        return true;
    } else if (name == "DestroyApp") {
        std::string channelId;
        if (!arguments.empty() && arguments.at(0)->IsString()) {
            channelId = arguments.at(0)->GetStringValue().ToString();
        }

        DEBUG("V8Handler::Execute DestroyApp ChannelId {}", channelId);
        sendMessageToProcess("DestroyApp", channelId);

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
    } else if (name == "StreamEventFilter") {
        bool result = false;
        if (arguments.size() >= 2) {
            result = vdrClient->StreamEventFilter(arguments.at(0)->GetIntValue(), arguments.at(1)->GetBoolValue());
        }
        retval = CefV8Value::CreateBool(result);
        return true;
    } else if (name == "SelectSubtitleTrack") {
        bool result = false;
        if (!arguments.empty()) {
            // -1 switches the subtitles off
            result = vdrClient->SelectSubtitleTrack(std::to_string(arguments.at(0)->GetIntValue()));
        }
        retval = CefV8Value::CreateBool(result);
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
