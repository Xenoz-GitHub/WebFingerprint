#include "webfingerprint/engine/engine.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

#include "webfingerprint/engine/technologies.h"
#include "webfingerprint/utils/string.h"

namespace wf::engine {
namespace {

bool ci_equals(std::string_view a, std::string_view b) {
    return wf::ascii_lower(a) == wf::ascii_lower(b);
}

bool ci_contains(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    const auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                                [](char a, char b) {
                                    return std::tolower(static_cast<unsigned char>(a)) ==
                                           std::tolower(static_cast<unsigned char>(b));
                                });
    return it != haystack.end();
}

std::string extract_version(std::string_view text, std::string_view prefix) {
    const size_t pos = text.find(prefix);
    if (pos == std::string_view::npos) {
        return {};
    }
    std::string version;
    for (char c : text.substr(pos + prefix.size())) {
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
            version += c;
        } else if (!version.empty()) {
            break;
        }
    }
    while (!version.empty() && version.back() == '.') {
        version.pop_back();
    }
    return version;
}

bool match_rule(const Rule& rule, const Evidence& evidence, std::string& matched_text) {
    switch (rule.type) {
        case MatchType::HeaderPresent: {
            const auto value = evidence.header(rule.target);
            if (value) {
                matched_text = *value;
                return true;
            }
            return false;
        }
        case MatchType::HeaderEquals: {
            const auto value = evidence.header(rule.target);
            if (value && ci_equals(*value, rule.pattern)) {
                matched_text = *value;
                return true;
            }
            return false;
        }
        case MatchType::HeaderContains: {
            const auto value = evidence.header(rule.target);
            if (value && ci_contains(*value, rule.pattern)) {
                matched_text = *value;
                return true;
            }
            return false;
        }
        case MatchType::BodyContains: {
            if (ci_contains(evidence.body, rule.pattern)) {
                matched_text = evidence.body;
                return true;
            }
            return false;
        }
        case MatchType::MetaGeneratorContains: {
            for (const auto& content : evidence.meta_generators) {
                if (ci_contains(content, rule.pattern)) {
                    matched_text = content;
                    return true;
                }
            }
            return false;
        }
        case MatchType::ScriptSrcContains: {
            for (const auto& src : evidence.script_srcs) {
                if (ci_contains(src, rule.pattern)) {
                    matched_text = src;
                    return true;
                }
            }
            return false;
        }
        case MatchType::StylesheetHrefContains: {
            for (const auto& href : evidence.stylesheet_hrefs) {
                if (ci_contains(href, rule.pattern)) {
                    matched_text = href;
                    return true;
                }
            }
            return false;
        }
        case MatchType::LinkHrefContains: {
            for (const auto& href : evidence.anchor_links) {
                if (ci_contains(href, rule.pattern)) {
                    matched_text = href;
                    return true;
                }
            }
            return false;
        }
        case MatchType::CookieNameEquals: {
            for (const auto& name : evidence.cookie_names) {
                if (ci_equals(name, rule.pattern)) {
                    matched_text = name;
                    return true;
                }
            }
            return false;
        }
        case MatchType::UrlContains: {
            if (ci_contains(evidence.url, rule.pattern)) {
                matched_text = evidence.url;
                return true;
            }
            return false;
        }
        case MatchType::ScriptsPresent: {
            if (!evidence.script_srcs.empty() || evidence.has_inline_script) {
                matched_text = rule.pattern;
                return true;
            }
            return false;
        }
        case MatchType::StyleSheetsPresent: {
            if (!evidence.stylesheet_hrefs.empty()) {
                matched_text = rule.pattern;
                return true;
            }
            return false;
        }
        case MatchType::Html5DoctypePresent: {
            if (evidence.has_html5_doctype) {
                matched_text = rule.pattern;
                return true;
            }
            return false;
        }
    }
    return false;
}

}

FingerprintEngine::FingerprintEngine() : FingerprintEngine(builtin_technologies()) {}

FingerprintEngine::FingerprintEngine(std::vector<TechnologyDef> technologies)
    : technologies_(std::move(technologies)) {}

void FingerprintEngine::add(TechnologyDef technology) {
    technologies_.push_back(std::move(technology));
}

std::vector<Technology> FingerprintEngine::analyze(const Evidence& evidence) const {
    std::vector<Technology> results;

    for (const auto& definition : technologies_) {
        std::vector<std::pair<const Rule*, std::string>> matched;
        for (const auto& rule : definition.rules) {
            std::string matched_text;
            if (match_rule(rule, evidence, matched_text)) {
                matched.emplace_back(&rule, std::move(matched_text));
            }
        }

        if (matched.empty()) {
            continue;
        }
        if (definition.requires_all && matched.size() != definition.rules.size()) {
            continue;
        }

        double confidence = 1.0;
        for (const auto& [rule, text] : matched) {
            confidence *= 1.0 - rule->weight;
        }
        confidence = 1.0 - confidence;

        if (confidence < definition.min_confidence) {
            continue;
        }

        Technology technology;
        technology.name = definition.name;
        technology.category = definition.category;
        technology.confidence = confidence;
        for (const auto& [rule, text] : matched) {
            technology.evidence.push_back(rule->name);
            if (technology.version.empty() && !rule->version_prefix.empty()) {
                technology.version = extract_version(text, rule->version_prefix);
            }
        }

        results.push_back(std::move(technology));
    }

    std::sort(results.begin(), results.end(), [](const Technology& a, const Technology& b) {
        if (a.confidence != b.confidence) {
            return a.confidence > b.confidence;
        }
        return a.name < b.name;
    });

    return results;
}

}
