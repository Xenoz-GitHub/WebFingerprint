#include "webfingerprint/utils/html.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>

#include "webfingerprint/utils/string.h"

namespace wf::utils {
namespace {

std::string decode_entities(std::string_view input) {
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] != '&') {
            output += input[i];
            continue;
        }

        const size_t semi = input.find(';', i);
        if (semi == std::string_view::npos || semi - i > 12) {
            output += input[i];
            continue;
        }

        const std::string_view entity = input.substr(i + 1, semi - i - 1);
        if (!entity.empty() && entity[0] == '#') {
            int code = 0;
            bool valid = false;
            if (entity.size() > 1 && (entity[1] == 'x' || entity[1] == 'X')) {
                for (char c : entity.substr(2)) {
                    int digit = 0;
                    if (c >= '0' && c <= '9') {
                        digit = c - '0';
                    } else if (c >= 'a' && c <= 'f') {
                        digit = c - 'a' + 10;
                    } else if (c >= 'A' && c <= 'F') {
                        digit = c - 'A' + 10;
                    } else {
                        digit = -1;
                    }
                    if (digit < 0) {
                        valid = false;
                        break;
                    }
                    code = code * 16 + digit;
                    valid = true;
                }
            } else {
                for (char c : entity.substr(1)) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        valid = false;
                        break;
                    }
                    code = code * 10 + (c - '0');
                    valid = true;
                }
            }
            if (valid && code > 0 && code <= 0x10FFFF) {
                if (code < 0x80) {
                    output += static_cast<char>(code);
                } else if (code < 0x800) {
                    output += static_cast<char>(0xC0 | (code >> 6));
                    output += static_cast<char>(0x80 | (code & 0x3F));
                } else if (code < 0x10000) {
                    output += static_cast<char>(0xE0 | (code >> 12));
                    output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    output += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    output += static_cast<char>(0xF0 | (code >> 18));
                    output += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
                    output += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    output += static_cast<char>(0x80 | (code & 0x3F));
                }
                i = semi;
                continue;
            }
            output += input[i];
            continue;
        }

        static constexpr std::array<std::pair<std::string_view, char>, 7> kEntities = {{
            {"amp", '&'},
            {"lt", '<'},
            {"gt", '>'},
            {"quot", '"'},
            {"apos", '\''},
            {"nbsp", ' '},
            {"copy", '\xA9'},
        }};
        bool decoded = false;
        for (const auto& [name, value] : kEntities) {
            if (entity == name) {
                output += value;
                i = semi;
                decoded = true;
                break;
            }
        }
        if (!decoded) {
            output += input[i];
        }
    }
    return output;
}

bool ci_eq(std::string_view a, std::string_view b) {
    return a.size() == b.size() && wf::ascii_lower(a) == wf::ascii_lower(b);
}

bool is_whitespace(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f';
}

bool parse_attr_value(std::string_view html, size_t& pos, std::string& out) {
    while (pos < html.size() && is_whitespace(html[pos])) {
        ++pos;
    }
    if (pos >= html.size()) {
        return false;
    }

    if (html[pos] == '"' || html[pos] == '\'') {
        const char quote = html[pos];
        ++pos;
        const size_t end = html.find(quote, pos);
        if (end == std::string_view::npos) {
            out = decode_entities(html.substr(pos));
            pos = html.size();
        } else {
            out = decode_entities(html.substr(pos, end - pos));
            pos = end + 1;
        }
        return true;
    }

    const size_t start = pos;
    while (pos < html.size() && !is_whitespace(html[pos]) && html[pos] != '>' &&
           html[pos] != '/') {
        ++pos;
    }
    if (pos == start) {
        return false;
    }
    out = decode_entities(html.substr(start, pos - start));
    return true;
}

bool parse_attribute(std::string_view html, size_t& pos, std::string& name, std::string& value) {
    while (pos < html.size() && is_whitespace(html[pos])) {
        ++pos;
    }
    if (pos >= html.size() || html[pos] == '>' || html[pos] == '/') {
        return false;
    }

    const size_t name_start = pos;
    while (pos < html.size() && !is_whitespace(html[pos]) && html[pos] != '=' &&
           html[pos] != '>' && html[pos] != '/') {
        ++pos;
    }
    if (pos == name_start) {
        return false;
    }
    name = ascii_lower(html.substr(name_start, pos - name_start));

    while (pos < html.size() && is_whitespace(html[pos])) {
        ++pos;
    }
    if (pos < html.size() && html[pos] == '=') {
        ++pos;
        value.clear();
        parse_attr_value(html, pos, value);
    } else {
        value.clear();
    }
    return true;
}

void skip_raw_text(std::string_view html, size_t& pos, std::string_view tag) {
    const std::string close_tag = "</" + std::string(tag);
    const size_t end = ascii_lower(html.substr(pos)).find(close_tag);
    if (end == std::string_view::npos) {
        pos = html.size();
        return;
    }
    pos += end;
}

}

HtmlInfo analyze_html(std::string_view body) {
    HtmlInfo info;

    size_t pos = 0;
    bool in_title = false;
    std::string title_text;

    while (pos < body.size()) {
        const size_t open = body.find('<', pos);
        if (open == std::string_view::npos) {
            break;
        }

        if (in_title) {
            title_text.append(body.substr(pos, open - pos));
        }

        if (body.substr(open, 4) == "<!--") {
            const size_t close = body.find("-->", open + 4);
            pos = close == std::string_view::npos ? body.size() : close + 3;
            continue;
        }

        if (body.substr(open, 9) == "<!DOCTYPE" || body.substr(open, 8) == "<!doctype") {
            const size_t close = body.find('>', open);
            if (close != std::string_view::npos &&
                wf::ascii_lower(body.substr(open, close - open)).find("html") !=
                    std::string::npos) {
                info.has_html5_doctype = true;
            }
            pos = close == std::string_view::npos ? body.size() : close + 1;
            continue;
        }

        if (open + 1 < body.size() && body[open + 1] == '/') {
            const size_t name_start = open + 2;
            size_t name_end = name_start;
            while (name_end < body.size() && !is_whitespace(body[name_end]) &&
                   body[name_end] != '>') {
                ++name_end;
            }
            const std::string tag_name = ascii_lower(body.substr(name_start, name_end - name_start));
            if (tag_name == "title") {
                in_title = false;
            }
            const size_t close = body.find('>', name_end);
            pos = close == std::string_view::npos ? body.size() : close + 1;
            continue;
        }

        size_t tag_end = open + 1;
        while (tag_end < body.size() && !is_whitespace(body[tag_end]) && body[tag_end] != '>' &&
               body[tag_end] != '/') {
            ++tag_end;
        }
        const std::string tag_name = ascii_lower(body.substr(open + 1, tag_end - open - 1));

        if (tag_name.empty()) {
            pos = open + 1;
            continue;
        }

        if (tag_name == "script" || tag_name == "style") {
            const bool is_script_tag = tag_name == "script";
            size_t attr_pos = tag_end;
            std::string attr_name;
            std::string attr_value;
            std::string src;

            while (attr_pos < body.size() && body[attr_pos] != '>') {
                if (!parse_attribute(body, attr_pos, attr_name, attr_value)) {
                    break;
                }
                if (attr_name == "src") {
                    src = attr_value;
                }
            }

            if (is_script_tag) {
                if (!src.empty()) {
                    info.scripts.push_back(src);
                } else {
                    info.has_inline_script = true;
                }
            }

            const size_t close = body.find('>', attr_pos);
            if (close == std::string_view::npos) {
                pos = body.size();
                break;
            }

            size_t content_pos = close + 1;
            skip_raw_text(body, content_pos, tag_name);
            pos = content_pos;
            continue;
        }

        if (tag_name == "title") {
            in_title = true;
            const size_t close = body.find('>', tag_end);
            pos = close == std::string_view::npos ? body.size() : close + 1;
            continue;
        }

        const bool is_meta = tag_name == "meta";
        const bool is_link = tag_name == "link";
        const bool is_anchor = tag_name == "a";

        if (is_meta || is_link || is_anchor) {
            size_t attr_pos = tag_end;
            std::string attr_name;
            std::string attr_value;
            std::string href;
            std::string rel;
            MetaTag meta;

            while (attr_pos < body.size() && body[attr_pos] != '>') {
                if (!parse_attribute(body, attr_pos, attr_name, attr_value)) {
                    break;
                }
                if (attr_name == "href") {
                    href = attr_value;
                } else if (attr_name == "rel") {
                    rel = attr_value;
                } else if (is_meta && attr_name == "name") {
                    meta.name = attr_value;
                } else if (is_meta && attr_name == "content") {
                    meta.content = attr_value;
                } else if (is_meta && attr_name == "charset") {
                    meta.name = "charset";
                    meta.content = attr_value;
                } else if (is_meta && attr_name == "http-equiv") {
                    meta.name = "http-equiv";
                } else if (is_meta && attr_name == "property") {
                    meta.name = attr_value;
                }
            }

            const size_t close = body.find('>', attr_pos);
            pos = close == std::string_view::npos ? body.size() : close + 1;

            if (is_meta && !meta.name.empty()) {
                info.meta.push_back(std::move(meta));
            }
            if (is_link && !href.empty()) {
                if (wf::ascii_lower(rel).find("stylesheet") != std::string::npos) {
                    info.stylesheets.push_back(href);
                }
            }
            if (is_anchor && !href.empty()) {
                info.links.push_back(href);
            }
            continue;
        }

        const size_t close = body.find('>', tag_end);
        pos = close == std::string_view::npos ? body.size() : close + 1;
    }

    if (in_title) {
        title_text.append(body.substr(pos));
    }
    info.title = decode_entities(title_text);
    return info;
}

}
