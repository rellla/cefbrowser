#pragma once

#include <vector>
#include <mutex>
#include <thread>
#include <condition_variable>
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
    OsdSender(VdrClient *client, bool qoi);
    ~OsdSender();

    // called from OnPaint()
    void Paint(const void *buffer, int width, int height, const std::vector<CefRect> &rects);

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
};
