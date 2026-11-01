#include <cstring>
#include <algorithm>
#include "osdsender.h"
#include "logger.h"
#include "qoi.h"

// more dirty regions are merged into their bounding box
#define MAX_DIRTY_RECTS 16

// color of the regions where the video shall be visible
#define VIDEO_KEY_COLOR    0xfffe2e9a
#define VIDEO_TRANSPARENT  0x00fe2e9a

// measurement: the last key sent to the page
static std::mutex keyMutex;
static std::string keyName;
static std::chrono::steady_clock::time_point keyTime;

void OsdSender::KeyPressed(const std::string &key) {
    std::lock_guard<std::mutex> lock(keyMutex);
    keyName = key;
    keyTime = std::chrono::steady_clock::now();
}

static double ms(std::chrono::steady_clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
}

OsdSender::OsdSender(VdrClient *client, bool qoi, bool measure) : vdrClient(client), useQoi(qoi), measure(measure) {
    statsStart = clock::now();
    thread = std::thread(&OsdSender::Run, this);
}

OsdSender::~OsdSender() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stop = true;
    }
    condition.notify_all();

    if (thread.joinable()) {
        thread.join();
    }
}

void OsdSender::AddDirty(const CefRect &rect) {
    // a region inside an existing one adds nothing
    for (const auto &d : dirty) {
        if (rect.x >= d.x && rect.y >= d.y && rect.x + rect.width <= d.x + d.width && rect.y + rect.height <= d.y + d.height) {
            return;
        }
    }

    dirty.push_back(rect);

    if (dirty.size() > MAX_DIRTY_RECTS) {
        CefRect box = dirty[0];
        for (const auto &d : dirty) {
            int x2 = std::max(box.x + box.width, d.x + d.width);
            int y2 = std::max(box.y + box.height, d.y + d.height);
            box.x = std::min(box.x, d.x);
            box.y = std::min(box.y, d.y);
            box.width = x2 - box.x;
            box.height = y2 - box.y;
        }
        dirty.clear();
        dirty.push_back(box);
    }
}

void OsdSender::Paint(const void *buffer, int width, int height, const std::vector<CefRect> &rects) {
    {
        std::lock_guard<std::mutex> lock(mutex);

        if (measure) {
            auto now = clock::now();
            if (dirty.empty()) {
                firstPaint = now;
            }

            // the first frame after a key
            std::lock_guard<std::mutex> keyLock(keyMutex);
            if (!keyName.empty() && keyPaintName.empty()) {
                keyPaintName = keyName;
                keyPaint = now;
                INFO("[measure] key {}: first frame rendered after {:.1f} ms", keyName, ms(now - keyTime));
            }
        }

        // a new size: the whole frame is new
        std::vector<CefRect> regions = rects;
        if (width != frameWidth || height != frameHeight) {
            frame.assign((size_t) width * height, 0);
            frameWidth = width;
            frameHeight = height;
            dirty.clear();
            regions.clear();
            regions.emplace_back(0, 0, width, height);
        }

        for (auto r : regions) {
            // clip to the frame
            r.x = std::max(0, r.x);
            r.y = std::max(0, r.y);
            r.width = std::min(r.width, width - r.x);
            r.height = std::min(r.height, height - r.y);
            if (r.width <= 0 || r.height <= 0) {
                continue;
            }

            const uint32_t *src = (const uint32_t *) buffer + (size_t) r.y * width + r.x;
            uint32_t *dst = frame.data() + (size_t) r.y * width + r.x;

            for (int y = 0; y < r.height; ++y) {
                for (int x = 0; x < r.width; ++x) {
                    // the video shall be visible in these parts of the OSD
                    uint32_t c = src[x];
                    dst[x] = (c == VIDEO_KEY_COLOR) ? VIDEO_TRANSPARENT : c;
                }
                src += width;
                dst += width;
            }

            AddDirty(r);
        }
    }

    condition.notify_one();
}

void OsdSender::Run() {
    while (true) {
        int width, height;
        clock::time_point waitStart;
        std::vector<CefRect> rects;
        std::vector<std::string> data;

        {
            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [this] { return stop || !dirty.empty(); });

            if (stop) {
                return;
            }

            rects.swap(dirty);
            width = frameWidth;
            height = frameHeight;
            waitStart = firstPaint;

            // copy the regions, the frame may change while sending
            for (const auto &r : rects) {
                std::string region((size_t) r.width * r.height * 4, '\0');
                uint32_t *dst = (uint32_t *) region.data();
                const uint32_t *src = frame.data() + (size_t) r.y * width + r.x;

                for (int y = 0; y < r.height; ++y) {
                    memcpy(dst, src, (size_t) r.width * 4);
                    src += width;
                    dst += r.width;
                }

                data.push_back(std::move(region));
            }
        }

        size_t bytes = 0;
        for (const auto &d : data) {
            bytes += d.size();
        }

        auto sendStart = clock::now();
        Send(width, height, rects, data);

        if (measure) {
            Measure(waitStart, sendStart, (int) rects.size(), bytes);
        }
    }
}

void OsdSender::Measure(clock::time_point waitStart, clock::time_point sendStart, int rects, size_t bytes) {
    auto now = clock::now();
    double send = ms(now - sendStart);

    // the frame of a key has arrived at VDR
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!keyPaintName.empty()) {
            std::lock_guard<std::mutex> keyLock(keyMutex);
            INFO("[measure] key {}: frame at VDR after {:.1f} ms (sending {:.1f} ms)", keyPaintName, ms(now - keyTime), send);
            if (keyName == keyPaintName) {
                keyName.clear();
            }
            keyPaintName.clear();
        }
    }

    statsFrames++;
    statsRects += rects;
    statsBytes += bytes;
    statsWait += ms(sendStart - waitStart);
    statsSend += send;
    statsSendMax = std::max(statsSendMax, send);

    double elapsed = ms(now - statsStart);
    if (elapsed >= 5000) {
        INFO("[measure] OSD: {} frames in {:.1f} s, {:.1f} regions/frame, {:.1f} MB, wait {:.1f} ms, send avg {:.1f} max {:.1f} ms",
             statsFrames, elapsed / 1000, (double) statsRects / statsFrames, statsBytes / 1048576.0,
             statsWait / statsFrames, statsSend / statsFrames, statsSendMax);

        statsStart = now;
        statsFrames = statsRects = 0;
        statsBytes = 0;
        statsWait = statsSend = statsSendMax = 0;
    }
}

void OsdSender::Send(int width, int height, const std::vector<CefRect> &rects, std::vector<std::string> &data) {
    // all regions of the frame in one request, VDR flushes the OSD once
    std::vector<OsdRectType> osdRects;
    osdRects.reserve(rects.size());

    for (size_t i = 0; i < rects.size(); ++i) {
        const auto &r = rects[i];

        OsdRectType rect;
        rect.x = r.x;
        rect.y = r.y;
        rect.width = r.width;
        rect.height = r.height;

        if (useQoi) {
            qoi_desc desc {
                .width = static_cast<unsigned int>(r.width),
                .height = static_cast<unsigned int>(r.height),
                .channels = 4,
                .colorspace = QOI_LINEAR
            };

            int out_len = 0;
            char *encoded = static_cast<char *>(qoi_encode(data[i].data(), &desc, &out_len));
            if (encoded == nullptr) {
                continue;
            }
            rect.data.assign(encoded, out_len);
            free(encoded);
        } else {
            rect.data.swap(data[i]);
        }

        osdRects.push_back(std::move(rect));
    }

    if (!osdRects.empty()) {
        vdrClient->ProcessOsdFrame(width, height, useQoi, osdRects);
    }
}
