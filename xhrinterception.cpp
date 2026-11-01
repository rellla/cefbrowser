#include "xhrinterception.h"
#include "tools.h"
#include "json.hpp"
#include "logger.h"

void XhrInterception::modifyResponse() {
    nlohmann::json dataJson;

    if (cprResponse.status_code < 300) {
        DEBUG("[XhrInterception] Request Completed {} in {} ms", cprResponse.url.str(), cprResponse.elapsed);

        // hbbtv.zdf.de
        if (logger->isTraceEnabled()) {
            TRACE("[XhrInterception] Downloaded data:\n{}\n", cprResponse.text);
        }

        try {
            dataJson = nlohmann::ordered_json::parse(cprResponse.text);
        } catch (nlohmann::json::parse_error &e) {
            INFO("[XhrInterception] Json parse error: {}", e.what());
            return;
        }

        auto j_flat = dataJson.flatten();
        if (logger->isTraceEnabled()) {
            TRACE("[XhrInterception] Json (flatten):\n{}", to_string(j_flat));
        }

        // ZDF special handling: the video URLs are hidden from the dash player
        if (browserOptions.zdfQuirks && (cprResponse.url.str().find("new-hbbtv.zdf.de/al/video-pages/by-canonical") != std::string::npos) && (cprResponse.url.str().find("livestreams=true") != std::string::npos)) {
            for (auto& el : j_flat.items()) {
                // std::cout << "key: " << el.key() << ", value:" << el.value() << '\n';
                if (el.key().find("h264_aac_mp4_http_mpd_http") != std::string::npos && el.key().find("url") != std::string::npos) {
                    TRACE("[XhrInterception] Found live URL: {}", el.value().get<std::string>());
                    auto url = el.value().get<std::string>();
                    auto urlEncoded = CefBase64Encode(url.c_str(), url.length());

                    j_flat[el.key()] = std::string("//localhost/encoded/") + urlEncoded.ToString() + ".mp4";
                }
            }

            j_flat.erase("/data/videoCluster/teaser/nodes");

            dataJson = j_flat.unflatten();
        }

        // Only look at existing keys: operator[] inserts missing keys as null (the
        // application then gets e.g. "data": null) and throws if the json is no object.
        auto isObject = [](const nlohmann::ordered_json& j, const char* key) {
            return j.is_object() && j.contains(key) && j[key].is_object();
        };

        if (cprResponse.url.str().find("-hbbtv.zdf.de/ds/configuration") != std::string::npos) {
            if (browserOptions.zdfQuirks && dataJson.is_object() && dataJson.contains("dash")) {
                dataJson["dash"] = false;
            }
        } else if (cprResponse.url.str().find("hbbtv.zdf.de") != std::string::npos) {
            // Version 1
            if (isObject(dataJson, "data")) {
                dataJson["data"].erase("ageControl");
            }

            // Version 2
            if (dataJson.is_object() && dataJson.contains("fsk")) {
                dataJson.erase("fsk");
            }

            // Version 3
            if (isObject(dataJson, "videoMetaData")) {
                dataJson["videoMetaData"].erase("fsk");
            }
        }
    } else {
        DEBUG("[XhrInterception] Error {}, Code {}, Text {} in {} ms", cprResponse.url.str(), cprResponse.status_code, cprResponse.status_line, cprResponse.elapsed);
        return;
    }

    cprResponse.header["Access-Control-Allow-Origin"] = "*";

    // save downloaded data
    cprResponse.text = dataJson.dump();
}