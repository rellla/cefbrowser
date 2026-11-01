#include <vector>
#include "tools.h"
#include "pagemodifier.h"

void PageModifier::init(std::string sp) {
    staticPath = std::move(sp);
}

std::string PageModifier::insertAfterHead(std::string &origStr, std::string &addStr) {
    std::size_t found = origStr.find("<head");
    if (found == std::string::npos) {
        return origStr;
    }

    found = origStr.find(">", found + 1, 1);
    if (found == std::string::npos) {
        return origStr;
    }

    return origStr.substr(0, found + 1) + addStr + origStr.substr(found + 1);
}

std::string PageModifier::insertBeforeEnd(std::string &origStr, std::string &addStr) {
    std::size_t found = origStr.find("</body>");
    if (found == std::string::npos) {
        return origStr;
    }

    return origStr.substr(0, found) + addStr + origStr.substr(found);
}

std::string PageModifier::getDynamicJs() {
    std::string dynamicJs = readFile((staticPath + "/js/_dynamic.js").c_str());
    return std::string("\n<script type=\"text/javascript\">\n//<![CDATA[\n") + dynamicJs + "\n// ]]>\n</script>\n";
}

std::string PageModifier::getDynamicZoom() {
    std::string dynamicJs = readFile((staticPath + "/js/_zoom_level.js").c_str());
    return std::string("\n<script type=\"text/javascript\">\n//<![CDATA[\n") + dynamicJs + "\n// ]]>\n</script>\n";
}

std::string PageModifier::injectAll(std::string_view source) {
    std::string result = std::string{source};

    std::vector<std::string> preJs = {"simple-tinyduration.js", "mutation-summary.js", "init.js", "keyhandler.js" };
    if (browserOptions.mse) {
        preJs.push_back("mse.js");
    }
    for (const auto & file : preJs) {
        std::string insert = "\n<script type=\"text/javascript\" src=\"http://localhost/js/" + file + "\"></script>\n";
        result = insertAfterHead(result, insert);
    }

    std::string dyn1 = getDynamicJs();
    result = insertAfterHead(result, dyn1);

    // the options of the VDR setup, before all other scripts
    std::string options = std::string("\n<script type=\"text/javascript\">window._vdrOptions = { mse: ") +
                          (browserOptions.mse ? "true" : "false") + ", zdfQuirks: " +
                          (browserOptions.zdfQuirks ? "true" : "false") + " };</script>\n";
    result = insertAfterHead(result, options);

    std::string postJs[] = { "video_quirks.js", "avcomponents.js", "videoobserver.js", "hbbtv.js", "initlast.js" };
    for (const auto & file : postJs) {
        std::string insert = "\n<script type=\"text/javascript\" src=\"http://localhost/js/" + file + "\"></script>\n";
        result = insertBeforeEnd(result, insert);
    }

    std::string dyn2 = getDynamicZoom();
    result = insertBeforeEnd(result, dyn2);

    std::string preCss[] = { "TiresiasPCfont.css", "videoquirks.css" };
    for (const auto & file : preCss) {
        std::string insert = "\n<link rel=\"stylesheet\" href=\"http://localhost/css/" + file + "\"/>\n";
        result = insertAfterHead(result, insert);
    }

    TRACE("Inject all:\n{}", result);

    return result;
}