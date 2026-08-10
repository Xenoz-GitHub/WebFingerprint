#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "webfingerprint/http/header_list.h"
#include "webfingerprint/http/http_client.h"

namespace wf::engine {

struct Evidence {
    std::string url;
    int status_code = 0;
    wf::http::HeaderList headers;
    std::string body;
    std::string html_title;
    std::vector<std::string> meta_generators;
    std::vector<std::string> script_srcs;
    std::vector<std::string> stylesheet_hrefs;
    std::vector<std::string> anchor_links;
    std::vector<std::string> cookie_names;
    bool has_inline_script = false;
    bool has_html5_doctype = false;

    std::optional<std::string> header(std::string_view name) const;
};

Evidence collect_evidence(const wf::http::FetchResult& result);

}
