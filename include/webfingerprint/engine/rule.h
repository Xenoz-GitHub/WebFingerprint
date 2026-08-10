#pragma once

#include <string>
#include <vector>

namespace wf::engine {

enum class MatchType {
    HeaderPresent,
    HeaderEquals,
    HeaderContains,
    BodyContains,
    MetaGeneratorContains,
    ScriptSrcContains,
    StylesheetHrefContains,
    LinkHrefContains,
    CookieNameEquals,
    UrlContains,
    ScriptsPresent,
    StyleSheetsPresent,
    Html5DoctypePresent,
};

struct Rule {
    std::string name;
    MatchType type = MatchType::BodyContains;
    std::string target;
    std::string pattern;
    double weight = 0.8;
    std::string version_prefix;
};

struct TechnologyDef {
    std::string name;
    std::string category;
    std::vector<Rule> rules;
    bool requires_all = false;
    double min_confidence = 0.5;
};

struct Technology {
    std::string name;
    std::string category;
    double confidence = 0.0;
    std::string version;
    std::vector<std::string> evidence;
};

}
