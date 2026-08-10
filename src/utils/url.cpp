#include "webfingerprint/utils/url.h"

#include <cctype>
#include <string>

#include "webfingerprint/utils/string.h"

namespace wf {
namespace {

bool is_valid_scheme(std::string_view scheme) {
    if (scheme != "http" && scheme != "https") {
        return false;
    }
    return true;
}

bool split_host_port(std::string_view hostport, std::string& host, std::string& port_text) {
    const size_t bracket = hostport.find('[');
    const size_t colon = hostport.rfind(':');
    if (colon != std::string_view::npos &&
        (bracket == std::string_view::npos || colon > bracket)) {
        host = std::string(hostport.substr(0, colon));
        port_text = std::string(hostport.substr(colon + 1));
    } else {
        host = std::string(hostport);
        port_text.clear();
    }
    return true;
}

bool is_all_digits(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::optional<uint16_t> parse_port(std::string_view text) {
    if (!is_all_digits(text)) {
        return std::nullopt;
    }
    unsigned long value = 0;
    for (char c : text) {
        value = value * 10 + static_cast<unsigned long>(c - '0');
        if (value > 65535) {
            return std::nullopt;
        }
    }
    if (value == 0) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(value);
}

}

uint16_t Url::default_port() const {
    if (scheme == "http") {
        return 80;
    }
    if (scheme == "https") {
        return 443;
    }
    return 0;
}

std::string Url::to_string() const {
    std::string result = scheme + "://";
    if (host.find(':') != std::string::npos) {
        result += '[' + host + ']';
    } else {
        result += host;
    }
    if (port.has_value() && *port != default_port()) {
        result += ':' + std::to_string(*port);
    }
    if (!path.empty()) {
        result += path;
    }
    if (!query.empty()) {
        result += '?' + query;
    }
    if (!fragment.empty()) {
        result += '#' + fragment;
    }
    return result;
}

UrlParseResult parse_url(std::string_view input) {
    UrlParseResult result;
    const std::string target = trim(input);
    if (target.empty()) {
        result.error = "empty target";
        return result;
    }

    Url url;

    std::string_view rest = target;

    const size_t scheme_end = rest.find("://");
    if (scheme_end == std::string_view::npos) {
        url.scheme = "https";
    } else {
        url.scheme = ascii_lower(rest.substr(0, scheme_end));
        if (!is_valid_scheme(url.scheme)) {
            result.error = "unsupported scheme: " + url.scheme;
            return result;
        }
        rest = rest.substr(scheme_end + 3);
    }

    const size_t at = rest.find('@');
    if (at != std::string_view::npos) {
        result.error = "embedded credentials are not allowed";
        return result;
    }

    std::string host_text;
    std::string path_query_fragment;
    const size_t slash = rest.find('/');
    const size_t question = rest.find('?');
    const size_t hash = rest.find('#');

    size_t authority_end = rest.size();
    if (slash != std::string_view::npos && slash < authority_end) authority_end = slash;
    if (question != std::string_view::npos && question < authority_end) authority_end = question;
    if (hash != std::string_view::npos && hash < authority_end) authority_end = hash;

    host_text = std::string(rest.substr(0, authority_end));
    path_query_fragment = std::string(rest.substr(authority_end));

    if (host_text.empty()) {
        result.error = "missing host";
        return result;
    }

    if (host_text[0] == '[') {
        const size_t close = host_text.find(']');
        if (close == std::string::npos) {
            result.error = "unterminated IPv6 host";
            return result;
        }
        url.host = host_text.substr(1, close - 1);
        const std::string after = host_text.substr(close + 1);
        if (!after.empty()) {
            if (after[0] != ':') {
                result.error = "malformed IPv6 host";
                return result;
            }
            const auto port = parse_port(after.substr(1));
            if (!port) {
                result.error = "invalid port";
                return result;
            }
            url.port = port;
        }
    } else {
        std::string port_text;
        split_host_port(host_text, url.host, port_text);
        if (url.host.find(':') != std::string::npos) {
            result.error = "IPv6 hosts must be bracketed";
            return result;
        }
        if (host_text.find(':') != std::string::npos && port_text.empty()) {
            result.error = "invalid port";
            return result;
        }
        if (!port_text.empty()) {
            const auto port = parse_port(port_text);
            if (!port) {
                result.error = "invalid port";
                return result;
            }
            url.port = port;
        }
    }

    if (url.host.empty()) {
        result.error = "missing host";
        return result;
    }
    for (char c : url.host) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            result.error = "invalid host";
            return result;
        }
    }
    url.host = ascii_lower(url.host);

    const size_t q = path_query_fragment.find('?');
    const size_t f = path_query_fragment.find('#');
    std::string path;
    std::string query;
    std::string fragment;
    if (f != std::string_view::npos && q != std::string_view::npos && q < f) {
        path = path_query_fragment.substr(0, q);
        query = path_query_fragment.substr(q + 1, f - q - 1);
        fragment = path_query_fragment.substr(f + 1);
    } else if (q != std::string_view::npos) {
        path = path_query_fragment.substr(0, q);
        query = path_query_fragment.substr(q + 1);
    } else if (f != std::string_view::npos) {
        path = path_query_fragment.substr(0, f);
        fragment = path_query_fragment.substr(f + 1);
    } else {
        path = path_query_fragment;
    }

    if (path.empty()) {
        path = "/";
    }
    url.path = path;
    url.query = query;
    url.fragment = fragment;

    result.url = std::move(url);
    return result;
}

std::optional<Url> resolve_relative(const Url& base, std::string_view location) {
    const std::string loc = trim(location);
    if (loc.empty()) {
        return std::nullopt;
    }

    if (loc.find("://") != std::string::npos) {
        const auto parsed = parse_url(loc);
        if (!parsed.url) {
            return std::nullopt;
        }
        return parsed.url;
    }

    if (loc[0] == '#') {
        return std::nullopt;
    }

    Url result = base;
    result.fragment.clear();

    if (loc.rfind("//", 0) == 0) {
        const std::string full = base.scheme + ':' + loc;
        const auto parsed = parse_url(full);
        if (!parsed.url) {
            return std::nullopt;
        }
        return parsed.url;
    }

    if (loc[0] == '?') {
        result.query = loc.substr(1);
        return result;
    }

    if (loc[0] == '/') {
        const size_t q = loc.find('?');
        if (q != std::string::npos) {
            result.path = loc.substr(0, q);
            result.query = loc.substr(q + 1);
        } else {
            result.path = std::string(loc);
            result.query.clear();
        }
        return result;
    }

    std::string base_path = base.path;
    if (base_path.empty()) {
        base_path = "/";
    }
    const size_t last_slash = base_path.find_last_of('/');
    if (last_slash != std::string::npos) {
        result.path = base_path.substr(0, last_slash + 1) + std::string(loc);
    } else {
        result.path = "/" + std::string(loc);
    }
    const size_t q = result.path.find('?');
    if (q != std::string::npos) {
        result.query = result.path.substr(q + 1);
        result.path = result.path.substr(0, q);
    }
    return result;
}

}
