#include "webfingerprint/utils/string.h"

#include <algorithm>
#include <cctype>

namespace wf {

std::string ascii_lower(std::string_view s) {
    std::string result(s);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

bool starts_with_ci(std::string_view s, std::string_view prefix) {
    if (prefix.size() > s.size()) {
        return false;
    }
    return ascii_lower(s.substr(0, prefix.size())) == ascii_lower(prefix);
}

std::string trim(std::string_view s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return std::string(s.substr(begin, end - begin));
}

}
