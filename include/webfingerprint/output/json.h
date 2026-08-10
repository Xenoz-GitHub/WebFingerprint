#pragma once

#include <nlohmann/json.hpp>

#include <string_view>
#include <vector>

#include "webfingerprint/engine/evidence.h"
#include "webfingerprint/engine/rule.h"

namespace wf::output {

nlohmann::json scan_to_json(std::string_view request_url,
                            const wf::engine::Evidence& evidence,
                            const std::vector<wf::engine::Technology>& technologies);

}
