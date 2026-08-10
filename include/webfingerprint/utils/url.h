#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace wf {

struct Url {
    std::string scheme;
    std::string host;
    std::optional<uint16_t> port;
    std::string path;
    std::string query;
    std::string fragment;

    uint16_t default_port() const;
    std::string to_string() const;
};

struct UrlParseResult {
    std::optional<Url> url;
    std::string error;
};

UrlParseResult parse_url(std::string_view input);

std::optional<Url> resolve_relative(const Url& base, std::string_view location);

}
