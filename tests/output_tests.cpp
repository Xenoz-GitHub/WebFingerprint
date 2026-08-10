#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <string>
#include <utility>
#include <vector>

#include "webfingerprint/core/version.h"
#include "webfingerprint/engine/engine.h"
#include "webfingerprint/engine/evidence.h"
#include "webfingerprint/http/http_client.h"
#include "webfingerprint/output/json.h"
#include "webfingerprint/utils/url.h"

using wf::engine::Evidence;
using wf::engine::FingerprintEngine;
using wf::engine::Technology;
using wf::engine::collect_evidence;
using wf::output::scan_to_json;

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

}

TEST_CASE("scan_to_json emits tool and target metadata") {
    const Evidence evidence =
        evidence_from({{"Server", "nginx/1.25.3"}}, "<html><head><title>Demo</title></head></html>",
                      "https://example.com/start");

    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);

    const nlohmann::json doc = scan_to_json("https://example.com/start", evidence, technologies);

    CHECK(doc["tool"]["name"] == wf::kProjectName);
    CHECK(doc["tool"]["version"] == wf::kVersionString);
    CHECK(doc["target"]["request_url"] == "https://example.com/start");
    CHECK(doc["target"]["final_url"] == "https://example.com/start");
    CHECK(doc["target"]["status_code"] == 200);
    CHECK(doc["target"]["title"] == "Demo");
}

TEST_CASE("scan_to_json serializes detected technologies") {
    const Evidence evidence = evidence_from({{"Server", "nginx/1.25.3"}}, "hello");
    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);

    const nlohmann::json doc = scan_to_json("https://example.com/", evidence, technologies);

    REQUIRE(doc["technologies"].is_array());
    REQUIRE(doc["technologies"].size() == 1);

    const auto& nginx = doc["technologies"][0];
    CHECK(nginx["name"] == "nginx");
    CHECK(nginx["category"] == "web_server");
    CHECK(nginx["confidence"] == Catch::Approx(0.8));
    CHECK(nginx["version"] == "1.25.3");
    REQUIRE(nginx["evidence"].is_array());
    REQUIRE(nginx["evidence"].size() == 1);
    CHECK(nginx["evidence"][0] == "Server header mentions nginx");
}

TEST_CASE("serialized json round-trips through a parse and dump") {
    const Evidence evidence = evidence_from({}, "nothing here");
    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);

    const nlohmann::json doc = scan_to_json("https://example.com/x", evidence, technologies);
    const nlohmann::json reparsed = nlohmann::json::parse(doc.dump());

    CHECK(reparsed["target"]["request_url"] == "https://example.com/x");
    CHECK(reparsed["target"]["final_url"] == "https://example.com/");
    CHECK(reparsed["technologies"].empty());
}

TEST_CASE("an empty title and version are omitted, not null") {
    const Evidence evidence = evidence_from({{"Server", "nginx"}}, "x");
    FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);

    const nlohmann::json doc = scan_to_json("https://example.com/", evidence, technologies);

    CHECK_FALSE(doc["target"].contains("title"));
    CHECK_FALSE(doc["technologies"][0].contains("version"));
    CHECK(doc["technologies"][0]["confidence"].is_number());
}
