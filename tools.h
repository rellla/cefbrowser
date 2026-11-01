#pragma once

#include <string>
#include <atomic>

const std::string BLANK_PAGE = "http://localhost/internal/blank/page";

enum image_type_enum {
    NONE = 0,
    QOI = 1
};

typedef struct BrowserParameter {
    std::string vdrIp;
    int vdrPort;

    std::string transcoderIp;
    int transcoderPort;

    std::string browserIp;
    int browserPort;

    image_type_enum osdqoi;

    int zoom_width;
    int zoom_height;

    bool use_dirty_recs;

    std::string static_path;
    std::string user_agent_path;

    bool bindAll;
    bool measure = false;

    image_type_enum image_type;
} BrowserParameter;

bool endsWith(const std::string& str, const std::string& suffix);

// height and zoom level for a page width (1280, 1920, 2560, 3840). Returns false for other widths.
bool zoomForWidth(int width, int &height, double &zoomLevel);

// writes the script which sets the zoom of every page
void writeZoomScript(const std::string& staticPath, int width, int height, double zoomLevel);
bool startsWith(const std::string& str, const std::string& prefix);
std::string readFile(const char *filename);

// options of the VDR setup, used by the next page
struct BrowserOptions {
    std::atomic<bool> mse { true };          // Media Source Extensions for VDR
    std::atomic<bool> zdfQuirks { true };    // special handling of the ZDF applications
};

extern BrowserOptions browserOptions;
