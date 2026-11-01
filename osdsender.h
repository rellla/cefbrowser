#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <string>
#include "cef_includes.h"
#include "thrift-services/src-client/VdrClient.h"

// Sends the OSD to VDR in its own thread.
//
// OnPaint() runs in the UI thread of the browser. Sending synchronously blocked the
// browser (javascript, keys, the next frame) until VDR had drawn the image. Paint()
// only copies the changed regions into a frame buffer. The sender thread sends the
// latest state: frames which arrive while sending are merged.
class OsdSender {
public:
    OsdSender(VdrClient *client, bool qoi, bool measure = false);
    ~OsdSender();

    // called from OnPaint()
    void Paint(const void *buffer, int width, int height, const std::vector<CefRect> &rects);

    // measurement: a key has been sent to the page
    static void KeyPressed(const std::string &key);

private:
    void Run();
    void AddDirty(const CefRect &rect);
    void Send(int width, int height, const std::vector<CefRect> &rects, std::vector<std::string> &data);

    VdrClient *vdrClient;
    bool useQoi;

    std::mutex mutex;
    std::condition_variable condition;
    bool stop = false;

    std::vector<uint32_t> frame;      // the last frame of the browser
    int frameWidth = 0;
    int frameHeight = 0;
    std::vector<CefRect> dirty;       // regions not sent yet

    std::thread thread;

    // measurement (--measure)
    using clock = std::chrono::steady_clock;
    bool measure;
    clock::time_point firstPaint;          // first Paint() not sent yet
    clock::time_point keyPaint;            // first Paint() after a key
    std::string keyPaintName;
    clock::time_point statsStart;
    int statsFrames = 0;
    int statsRects = 0;
    size_t statsBytes = 0;
    double statsWait = 0;
    double statsSend = 0;
    double statsSendMax = 0;

    void Measure(clock::time_point waitStart, clock::time_point sendStart, int rects, size_t bytes);
};
