#include "webfingerprint/engine/technologies.h"

namespace wf::engine {
namespace {

Rule header_contains(std::string name, std::string evidence_text, std::string pattern,
                     double weight, std::string version_prefix = {}) {
    Rule rule;
    rule.name = std::move(evidence_text);
    rule.type = MatchType::HeaderContains;
    rule.target = std::move(name);
    rule.pattern = std::move(pattern);
    rule.weight = weight;
    rule.version_prefix = std::move(version_prefix);
    return rule;
}

Rule body_contains(std::string evidence_text, std::string pattern, double weight) {
    Rule rule;
    rule.name = std::move(evidence_text);
    rule.type = MatchType::BodyContains;
    rule.pattern = std::move(pattern);
    rule.weight = weight;
    return rule;
}

}

std::vector<TechnologyDef> builtin_technologies() {
    std::vector<TechnologyDef> technologies;

    technologies.push_back({
        "nginx",
        "web_server",
        {
            header_contains("Server", "Server header mentions nginx", "nginx", 0.8, "nginx/"),
        },
    });

    technologies.push_back({
        "Apache",
        "web_server",
        {
            header_contains("Server", "Server header starts with Apache", "apache/", 0.8, "Apache/"),
        },
    });

    technologies.push_back({
        "Cloudflare",
        "cdn",
        {
            header_contains("Server", "Server header mentions Cloudflare", "cloudflare", 0.9),
            {"CF-Ray request header present", MatchType::HeaderPresent, "CF-Ray", "", 0.7},
            {"cf-cache-status request header present", MatchType::HeaderPresent, "Cf-Cache-Status",
             "", 0.6},
        },
    });

    technologies.push_back({
        "React",
        "javascript_framework",
        {
            {"script src references react", MatchType::ScriptSrcContains, "", "react.", 0.9},
            body_contains("body contains data-reactroot", "data-reactroot", 0.8),
        },
    });

    technologies.push_back({
        "WordPress",
        "cms",
        {
            {"generator meta mentions WordPress", MatchType::MetaGeneratorContains, "", "wordpress",
             0.9, "WordPress "},
            body_contains("body contains wp-content/", "wp-content/", 0.7),
            body_contains("body contains wp-includes/", "wp-includes/", 0.7),
            {"link href references wp-content", MatchType::LinkHrefContains, "", "wp-content/", 0.6},
        },
    });

    return technologies;
}

}
