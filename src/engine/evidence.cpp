#include "webfingerprint/engine/evidence.h"

#include "webfingerprint/http/cookie.h"
#include "webfingerprint/utils/html.h"
#include "webfingerprint/utils/string.h"

namespace wf::engine {

std::optional<std::string> Evidence::header(std::string_view name) const {
    return headers.get(name);
}

Evidence collect_evidence(const wf::http::FetchResult& result) {
    Evidence evidence;
    if (!result.ok) {
        return evidence;
    }

    const auto& response = result.response;
    evidence.url = response.request_url.to_string();
    evidence.status_code = response.status_code;
    evidence.headers = response.headers;
    evidence.body = response.body;

    const auto html = wf::utils::analyze_html(response.body);
    evidence.html_title = html.title;
    for (const auto& meta : html.meta) {
        if (wf::ascii_lower(meta.name) == "generator") {
            evidence.meta_generators.push_back(meta.content);
        }
    }
    evidence.script_srcs = html.scripts;
    evidence.stylesheet_hrefs = html.stylesheets;
    evidence.anchor_links = html.links;

    for (const auto& cookie : wf::http::parse_cookies(response.headers)) {
        evidence.cookie_names.push_back(cookie.name);
    }

    return evidence;
}

}
