#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace wf::utils {

struct MetaTag {
    std::string name;
    std::string content;
};

struct HtmlInfo {
    std::string title;
    std::vector<MetaTag> meta;
    std::vector<std::string> scripts;
    std::vector<std::string> stylesheets;
    std::vector<std::string> links;
    bool has_inline_script = false;
    bool has_html5_doctype = false;
};

HtmlInfo analyze_html(std::string_view body);

}
