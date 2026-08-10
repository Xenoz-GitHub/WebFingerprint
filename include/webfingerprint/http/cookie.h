#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "webfingerprint/http/header_list.h"

namespace wf::http {

struct Cookie {
    std::string name;
    std::string value;
    std::string domain;
    std::string path;
    std::string expires;
    std::string max_age;
    std::string same_site;
    bool secure = false;
    bool http_only = false;
};

std::optional<Cookie> parse_set_cookie(std::string_view header_value);

std::vector<Cookie> parse_cookies(const HeaderList& headers);

}
