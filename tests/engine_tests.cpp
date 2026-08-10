#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

#include "webfingerprint/engine/engine.h"
#include "webfingerprint/engine/evidence.h"
#include "webfingerprint/http/http_client.h"
#include "webfingerprint/utils/url.h"
#include "test_server.h"

using wf::engine::Evidence;
using wf::engine::FingerprintEngine;
using wf::engine::Rule;
using wf::engine::Technology;
using wf::engine::TechnologyDef;
using wf::engine::collect_evidence;

namespace {

Evidence evidence_from(std::vector<std::pair<std::string, std::string>> headers,
                       std::string body,
                       std::string url = "https://example.com/") {
    wf::http::HttpResponse response;
    response.status_code = 200;
    response.status_text = "OK";
    for (auto& [name, value] : headers) {
        response.headers.add(std::move(name), std::move(value));
    }
    response.body = std::move(body);
    response.request_url = *wf::parse_url(url).url;

    wf::http::FetchResult result;
    result.ok = true;
    result.response = std::move(response);
    return collect_evidence(result);
}

const Technology& find_tech(const std::vector<Technology>& technologies, std::string_view name) {
    for (const auto& tech : technologies) {
        if (tech.name == name) {
            return tech;
        }
    }
    throw std::runtime_error("technology not found: " + std::string(name));
}

}

TEST_CASE("evidence is empty for a failed fetch") {
    wf::http::FetchResult result;
    result.ok = false;
    const Evidence evidence = collect_evidence(result);
    CHECK(evidence.url.empty());
    CHECK(evidence.status_code == 0);
    CHECK(evidence.body.empty());
}

TEST_CASE("header lookup is case-insensitive") {
    const Evidence evidence = evidence_from({{"Server", "nginx/1.25.3"}}, "body");
    REQUIRE(evidence.header("Server").has_value());
    CHECK(*evidence.header("Server") == "nginx/1.25.3");
    CHECK(*evidence.header("server") == "nginx/1.25.3");
    CHECK(*evidence.header("SERVER") == "nginx/1.25.3");
    CHECK_FALSE(evidence.header("X-Missing").has_value());
}

TEST_CASE("collect_evidence populates html and cookie fields") {
    const Evidence evidence = evidence_from(
        {{"Set-Cookie", "session=abc; HttpOnly"}, {"Server", "nginx"}},
        "<html><head><title>T</title>"
        "<meta name=\"generator\" content=\"WordPress 6.5\">"
        "<script src=\"/react.development.js\"></script>"
        "<link rel=\"stylesheet\" href=\"/main.css\">"
        "</head><body><a href=\"/page\">p</a></body></html>");

    CHECK(evidence.url == "https://example.com/");
    CHECK(evidence.status_code == 200);
    CHECK(evidence.html_title == "T");
    REQUIRE(evidence.meta_generators.size() == 1);
    CHECK(evidence.meta_generators[0] == "WordPress 6.5");
    REQUIRE(evidence.script_srcs.size() == 1);
    CHECK(evidence.script_srcs[0] == "/react.development.js");
    REQUIRE(evidence.stylesheet_hrefs.size() == 1);
    CHECK(evidence.stylesheet_hrefs[0] == "/main.css");
    REQUIRE(evidence.anchor_links.size() == 1);
    CHECK(evidence.anchor_links[0] == "/page");
    REQUIRE(evidence.cookie_names.size() == 1);
    CHECK(evidence.cookie_names[0] == "session");
}

TEST_CASE("nginx is detected from the Server header with version") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({{"Server", "nginx/1.18.0 (Ubuntu)"}}, "<html>ok</html>"));

    REQUIRE(technologies.size() == 1);
    const auto& nginx = technologies[0];
    CHECK(nginx.name == "nginx");
    CHECK(nginx.category == "web_server");
    CHECK(nginx.confidence == Catch::Approx(0.8));
    CHECK(nginx.version == "1.18.0");
    REQUIRE(nginx.evidence.size() == 1);
}

TEST_CASE("nginx is detected without a version when none is present") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence_from({{"Server", "nginx"}}, ""));

    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].version.empty());
}

TEST_CASE("apache is detected with version") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({{"Server", "Apache/2.4.41 (Ubuntu)"}}, ""));

    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].name == "Apache");
    CHECK(technologies[0].confidence == Catch::Approx(0.8));
    CHECK(technologies[0].version == "2.4.41");
}

TEST_CASE("apache-coyote (tomcat) does not match apache") {
    FingerprintEngine engine;
    const auto technologies =
        engine.analyze(evidence_from({{"Server", "Apache-Coyote/1.1"}}, ""));

    CHECK(technologies.empty());
}

TEST_CASE("cloudflare confidence combines across rules") {
    FingerprintEngine engine;

    const auto ray_only = engine.analyze(evidence_from({{"CF-Ray", "abc-ORD"}}, ""));
    REQUIRE(ray_only.size() == 1);
    CHECK(ray_only[0].name == "Cloudflare");
    CHECK(ray_only[0].confidence == Catch::Approx(0.7));

    const auto both = engine.analyze(
        evidence_from({{"CF-Ray", "abc-ORD"}, {"Server", "cloudflare"}}, ""));
    REQUIRE(both.size() == 1);
    CHECK(both[0].confidence == Catch::Approx(1.0 - 0.3 * 0.1));
    REQUIRE(both[0].evidence.size() == 2);
}

TEST_CASE("react is detected via script src and data-reactroot") {
    FingerprintEngine engine;

    const auto via_script = engine.analyze(
        evidence_from({}, "<html><head><script src=\"https://cdn.example.com/react.production.min.js\"></script></head></html>"));
    REQUIRE(via_script.size() == 1);
    CHECK(via_script[0].name == "React");
    CHECK(via_script[0].confidence == Catch::Approx(0.9));

    const auto via_root = engine.analyze(
        evidence_from({}, "<div id=\"root\" data-reactroot=\"\">hi</div>"));
    REQUIRE(via_root.size() == 1);
    CHECK(via_root[0].name == "React");
    CHECK(via_root[0].confidence == Catch::Approx(0.8));
}

TEST_CASE("wordpress is detected via generator meta with version") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({}, "<meta name=\"generator\" content=\"WordPress 6.5.2\" />"));

    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].name == "WordPress");
    CHECK(technologies[0].category == "cms");
    CHECK(technologies[0].confidence == Catch::Approx(0.9));
    CHECK(technologies[0].version == "6.5.2");
}

TEST_CASE("wordpress is detected via wp-content body marker") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({}, "<link rel=\"stylesheet\" href=\"https://site.com/wp-content/themes/twenty/css/main.css\">"));

    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].name == "WordPress");
    CHECK(technologies[0].confidence == Catch::Approx(0.7));
}

TEST_CASE("plain-text mentions of technology names do not trigger detections") {
    FingerprintEngine engine;

    const auto technologies = engine.analyze(evidence_from(
        {{"Server", "Microsoft-IIS/10.0"}},
        "<html><head><title>I love wordpress, nginx and react</title></head><body>"
        "cloudflare is a company</body></html>"));

    CHECK(technologies.empty());
}

TEST_CASE("header name casing does not matter") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({{"server", "nginx/1.2.3"}}, ""));

    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].name == "nginx");
}

TEST_CASE("multiple technologies are sorted by confidence descending") {
    FingerprintEngine engine;
    const auto technologies = engine.analyze(
        evidence_from({{"CF-Ray", "x-y"}, {"Server", "nginx"}},
                      "<meta name=\"generator\" content=\"WordPress 6.5\">"));

    REQUIRE(technologies.size() == 3);
    CHECK(technologies[0].name == "WordPress");
    CHECK(technologies[1].name == "nginx");
    CHECK(technologies[2].name == "Cloudflare");
}

TEST_CASE("empty evidence produces no technologies") {
    FingerprintEngine engine;
    CHECK(engine.analyze(Evidence{}).empty());
}

TEST_CASE("requires_all technologies need every rule to match") {
    TechnologyDef definition;
    definition.name = "CustomStack";
    definition.category = "test";
    definition.requires_all = true;
    definition.rules = {
        {"custom header A present", wf::engine::MatchType::HeaderPresent, "X-Custom-A", "", 0.7},
        {"custom header B present", wf::engine::MatchType::HeaderPresent, "X-Custom-B", "", 0.8},
    };

    FingerprintEngine engine;
    engine.add(std::move(definition));

    const auto partial = engine.analyze(evidence_from({{"X-Custom-A", "1"}}, ""));
    CHECK(partial.empty());

    const auto complete =
        engine.analyze(evidence_from({{"X-Custom-A", "1"}, {"X-Custom-B", "2"}}, ""));
    REQUIRE(complete.size() == 1);
    CHECK(complete[0].name == "CustomStack");
    CHECK(complete[0].confidence == Catch::Approx(1.0 - 0.3 * 0.2));
    CHECK(complete[0].evidence.size() == 2);
}

TEST_CASE("technologies below min_confidence are not reported") {
    TechnologyDef weak;
    weak.name = "WeakSignal";
    weak.category = "test";
    weak.rules = {{"body mentions marker", wf::engine::MatchType::BodyContains, "", "marker", 0.4}};

    FingerprintEngine engine;
    engine.add(std::move(weak));

    CHECK(engine.analyze(evidence_from({}, "has marker")).empty());

    TechnologyDef lenient_weak;
    lenient_weak.name = "WeakSignal";
    lenient_weak.category = "test";
    lenient_weak.rules = {{"body mentions marker", wf::engine::MatchType::BodyContains, "", "marker",
                           0.4}};
    lenient_weak.min_confidence = 0.3;
    FingerprintEngine lenient_engine;
    lenient_engine.add(std::move(lenient_weak));
    const auto technologies = lenient_engine.analyze(evidence_from({}, "has marker"));
    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].confidence == Catch::Approx(0.4));
}

TEST_CASE("custom rules match cookies and final url") {
    TechnologyDef definition;
    definition.name = "LoginTracker";
    definition.category = "test";
    definition.rules = {
        {"session cookie is set", wf::engine::MatchType::CookieNameEquals, "", "session", 0.6},
        {"url contains wp-login", wf::engine::MatchType::UrlContains, "", "wp-login.php", 0.5},
    };

    FingerprintEngine engine;
    engine.add(std::move(definition));

    const auto technologies = engine.analyze(evidence_from(
        {{"Set-Cookie", "session=abc123; HttpOnly"}}, "",
        "https://example.com/wp-login.php"));
    REQUIRE(technologies.size() == 1);
    CHECK(technologies[0].name == "LoginTracker");
    CHECK(technologies[0].confidence == Catch::Approx(1.0 - 0.4 * 0.5));
    CHECK(technologies[0].evidence.size() == 2);
}

TEST_CASE("full pipeline works against a real server") {
    wftest::TestServer server;
    server.add_route("/", {200,
                           "OK",
                           {{"Server", "nginx/1.24.0"}, {"Set-Cookie", "wordpress_test_cookie=1"}},
                           "<html><head><title>Blog</title>"
                           "<meta name=\"generator\" content=\"WordPress 6.4.3\">"
                           "<script src=\"/wp-includes/js/react.min.js\"></script>"
                           "</head><body>hello</body></html>"});
    server.start();

    wf::http::HttpClient client;
    wf::http::HttpRequest request;
    request.url = *wf::parse_url(server.base_url() + "/").url;
    const auto result = client.fetch(request);
    server.stop();

    REQUIRE(result.ok);
    const Evidence evidence = collect_evidence(result);
    CHECK(evidence.status_code == 200);

    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);
    REQUIRE(technologies.size() == 3);
    CHECK(technologies[0].name == "WordPress");
    CHECK(technologies[0].confidence == Catch::Approx(1.0 - 0.1 * 0.3));
    CHECK(technologies[0].version == "6.4.3");
    CHECK(technologies[1].name == "React");
    CHECK(technologies[2].name == "nginx");
    CHECK(technologies[2].version == "1.24.0");
}

TEST_CASE("evidence uses the final url after redirects") {
    wftest::TestServer server;
    server.add_route("/start",
                     {302, "Found", {{"Location", "/final"}}, ""});
    server.add_route("/final",
                     {200, "OK", {{"Server", "nginx"}}, "hello"});
    server.start();

    wf::http::HttpClient client;
    wf::http::HttpRequest request;
    request.url = *wf::parse_url(server.base_url() + "/start").url;
    const auto result = client.fetch(request);
    server.stop();

    REQUIRE(result.ok);
    const Evidence evidence = collect_evidence(result);
    CHECK(evidence.url == server.base_url() + "/final");
}
