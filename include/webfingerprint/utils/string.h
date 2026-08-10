#pragma once

#include <string>
#include <string_view>

namespace wf {

std::string ascii_lower(std::string_view s);

bool starts_with_ci(std::string_view s, std::string_view prefix);

std::string trim(std::string_view s);

}
