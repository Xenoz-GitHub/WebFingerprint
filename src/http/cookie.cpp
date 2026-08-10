#include "webfingerprint/http/cookie.h"

#include <string>

#include "webfingerprint/utils/string.h"

namespace wf::http {
namespace {

std::string_view ltrim_view(std::string_view s) {
    size_t begin = 0;
    while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t')) {
        ++begin;
    }
    return s.substr(begin);
}

std::string_view rtrim_view(std::string_view s) {
    size_t end = s.size();
    while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return s.substr(0, end);
}

bool is_token_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' ||
           c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' ||
           c == '~';
}

bool valid_cookie_name(std::string_view name) {
    for (char c : name) {
        if (!is_token_char(c)) {
            return false;
        }
    }
    return true;
}

bool ci_equals(std::string_view a, std::string_view b) {
    return wf::ascii_lower(a) == wf::ascii_lower(b);
}

}

std::optional<Cookie> parse_set_cookie(std::string_view header_value) {
    Cookie cookie;

    const size_t eq = header_value.find('=');
    if (eq == std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view name = rtrim_view(ltrim_view(header_value.substr(0, eq)));
    if (!valid_cookie_name(name)) {
        return std::nullopt;
    }
    cookie.name = std::string(name);

    std::string_view rest = ltrim_view(header_value.substr(eq + 1));
    if (!rest.empty() && (rest[0] == '"' || rest[0] == '\'')) {
        const char quote = rest[0];
        rest = rest.substr(1);
        const size_t end = rest.find(quote);
        if (end != std::string_view::npos) {
            cookie.value = std::string(rest.substr(0, end));
        } else {
            cookie.value = std::string(rest);
        }
    } else {
        const size_t semi = rest.find(';');
        if (semi != std::string_view::npos) {
            cookie.value = std::string(rtrim_view(rest.substr(0, semi)));
            rest = rest.substr(semi);
        } else {
            cookie.value = std::string(rtrim_view(rest));
            return cookie;
        }
    }

    size_t pos = 0;
    while (pos < rest.size()) {
        if (rest[pos] == ';') {
            ++pos;
            continue;
        }
        const size_t attr_end = rest.find(';', pos);
        const std::string_view part =
            attr_end == std::string_view::npos ? rest.substr(pos) : rest.substr(pos, attr_end - pos);
        pos = attr_end == std::string_view::npos ? rest.size() : attr_end + 1;

        const std::string_view trimmed = rtrim_view(ltrim_view(part));
        if (trimmed.empty()) {
            continue;
        }

        const size_t attr_eq = trimmed.find('=');
        const std::string_view attr_name =
            attr_eq == std::string_view::npos
                ? trimmed
                : rtrim_view(trimmed.substr(0, attr_eq));
        std::string_view attr_value =
            attr_eq == std::string_view::npos ? std::string_view{}
                                              : ltrim_view(trimmed.substr(attr_eq + 1));

        if (ci_equals(attr_name, "domain")) {
            cookie.domain = std::string(attr_value);
        } else if (ci_equals(attr_name, "path")) {
            cookie.path = std::string(attr_value);
        } else if (ci_equals(attr_name, "expires")) {
            cookie.expires = std::string(attr_value);
        } else if (ci_equals(attr_name, "max-age")) {
            cookie.max_age = std::string(attr_value);
        } else if (ci_equals(attr_name, "samesite")) {
            cookie.same_site = std::string(attr_value);
        } else if (ci_equals(attr_name, "secure")) {
            cookie.secure = true;
        } else if (ci_equals(attr_name, "httponly")) {
            cookie.http_only = true;
        }
    }

    return cookie;
}

std::vector<Cookie> parse_cookies(const HeaderList& headers) {
    std::vector<Cookie> cookies;
    for (const auto& value : headers.get_all("Set-Cookie")) {
        if (const auto cookie = parse_set_cookie(value)) {
            cookies.push_back(*cookie);
        }
    }
    return cookies;
}

}
