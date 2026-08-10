#include "webfingerprint/output/json.h"

#include <string>

#include "webfingerprint/core/version.h"

namespace wf::output {

nlohmann::json scan_to_json(std::string_view request_url,
                            const wf::engine::Evidence& evidence,
                            const std::vector<wf::engine::Technology>& technologies) {
    nlohmann::json root;

    root["tool"]["name"] = kProjectName;
    root["tool"]["version"] = kVersionString;

    root["target"]["request_url"] = std::string(request_url);
    root["target"]["final_url"] = evidence.url;
    root["target"]["status_code"] = evidence.status_code;
    if (!evidence.html_title.empty()) {
        root["target"]["title"] = evidence.html_title;
    }

    root["technologies"] = nlohmann::json::array();
    for (const auto& technology : technologies) {
        nlohmann::json entry;
        entry["name"] = technology.name;
        entry["category"] = technology.category;
        entry["confidence"] = technology.confidence;
        if (!technology.version.empty()) {
            entry["version"] = technology.version;
        }
        entry["evidence"] = technology.evidence;
        root["technologies"].push_back(std::move(entry));
    }

    return root;
}

}
