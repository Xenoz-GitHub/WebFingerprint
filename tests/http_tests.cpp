#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <optional>
#include <string>

#include "webfingerprint/http/header_list.h"
#include "webfingerprint/http/http_client.h"
#include "webfingerprint/utils/url.h"
#include "test_server.h"

using wf::parse_url;
using wf::http::FetchResult;
using wf::http::HeaderList;
using wf::http::HttpClient;
using wf::http::HttpErrorKind;
using wf::http::HttpRequest;
using wf::http::HttpVersion;

namespace {

struct ServerFixture {
    wftest::TestServer server;

    ServerFixture() { server.start(); }
    ~ServerFixture() { server.stop(); }

    HttpRequest request_for(const std::string& path) const {
        HttpRequest request;
        request.url = *parse_url(server.base_url() + path).url;
        return request;
    }
};

}

TEST_CASE("HeaderList lookups are case-insensitive and preserve duplicates") {
    HeaderList headers;
    headers.add("Server", "nginx-test");
    headers.add("Set-Cookie", "a=1");
    headers.add("Set-Cookie", "b=2");

    CHECK(headers.contains("server"));
    CHECK(headers.contains("SERVER"));
    CHECK(headers.get("Server") == std::optional<std::string>{"nginx-test"});
    CHECK(headers.get("server") == std::optional<std::string>{"nginx-test"});
    CHECK(headers.get("missing") == std::nullopt);
    REQUIRE(headers.get_all("set-cookie").size() == 2);
    CHECK(headers.get_all("Set-Cookie")[0] == "a=1");
    CHECK(headers.get_all("SET-COOKIE")[1] == "b=2");
}

TEST_CASE("GET returns status, headers, and body") {
    ServerFixture fixture;
    fixture.server.add_route("/", {200, "OK", {{"Content-Type", "text/html"}, {"Server", "nginx-test"}}, "<h1>hello</h1>"});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 200);
    CHECK(result.response.status_text == "OK");
    CHECK(result.response.version == HttpVersion::Http11);
    CHECK(result.response.headers.get("Server") == std::optional<std::string>{"nginx-test"});
    CHECK(result.response.headers.get("Content-Type") == std::optional<std::string>{"text/html"});
    CHECK(result.response.body == "<h1>hello</h1>");
    REQUIRE(result.response.redirect_chain.size() == 1);
    CHECK(result.response.redirect_chain[0] == fixture.server.base_url() + "/");
}

TEST_CASE("unknown paths return the default 404") {
    ServerFixture fixture;

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/nothing-here"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 404);
    CHECK(result.response.status_text == "Not Found");
}

TEST_CASE("relative redirect is followed and recorded in the chain") {
    ServerFixture fixture;
    fixture.server.add_route("/start", {301, "Moved Permanently", {{"Location", "/final"}}, ""});
    fixture.server.add_route("/final", {200, "OK", {}, "final body"});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/start"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 200);
    CHECK(result.response.body == "final body");
    CHECK(result.response.request_url.to_string() == fixture.server.base_url() + "/final");
    REQUIRE(result.response.redirect_chain.size() == 2);
    CHECK(result.response.redirect_chain[0] == fixture.server.base_url() + "/start");
    CHECK(result.response.redirect_chain[1] == fixture.server.base_url() + "/final");
}

TEST_CASE("multi-hop redirects are followed") {
    ServerFixture fixture;
    fixture.server.add_route("/a", {302, "Found", {{"Location", "/b/c"}}, ""});
    fixture.server.add_route("/b/c", {302, "Found", {{"Location", "/d?x=1"}}, ""});
    fixture.server.add_route("/d", {200, "OK", {}, "done"});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/a"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 200);
    CHECK(result.response.body == "done");
    REQUIRE(result.response.redirect_chain.size() == 3);
}

TEST_CASE("redirect loops are detected") {
    ServerFixture fixture;
    fixture.server.add_route("/a", {302, "Found", {{"Location", "/b"}}, ""});
    fixture.server.add_route("/b", {302, "Found", {{"Location", "/a"}}, ""});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/a"));

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::RedirectLoop);
}

TEST_CASE("the redirect limit is enforced") {
    ServerFixture fixture;
    for (int i = 0; i < 10; ++i) {
        fixture.server.add_route("/r" + std::to_string(i),
                                 {302, "Found", {{"Location", "/r" + std::to_string(i + 1)}}, ""});
    }
    fixture.server.add_route("/r10", {200, "OK", {}, "end"});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/r0"));

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::TooManyRedirects);
}

TEST_CASE("a 3xx without Location is returned as the final response") {
    ServerFixture fixture;
    fixture.server.add_route("/nolocation", {301, "Moved Permanently", {}, ""});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/nolocation"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 301);
    CHECK(result.response.redirect_chain.size() == 1);
}

TEST_CASE("redirects to unsupported schemes are not followed") {
    ServerFixture fixture;
    fixture.server.add_route("/ftp", {302, "Found", {{"Location", "ftp://example.com/x"}}, ""});

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/ftp"));

    REQUIRE(result.ok);
    CHECK(result.response.status_code == 302);
}

TEST_CASE("connection refused is reported") {
    wftest::TestServer server;
    const std::string url = server.base_url() + "/";
    server.stop();

    HttpClient client;
    HttpRequest request;
    request.url = *parse_url(url).url;
    const FetchResult result = client.fetch(request);

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::Connection);
}

TEST_CASE("request timeouts are reported") {
    ServerFixture fixture;
    fixture.server.add_delayed_route("/slow", std::chrono::milliseconds(2000), {200, "OK", {}, "late"});

    HttpClient client;
    HttpRequest request = fixture.request_for("/slow");
    request.total_timeout = std::chrono::seconds(1);
    const FetchResult result = client.fetch(request);

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::Timeout);
}

TEST_CASE("oversized responses are rejected") {
    ServerFixture fixture;
    const std::string big(3 * 1024 * 1024, 'a');
    fixture.server.add_route("/big", {200, "OK", {}, big});

    HttpClient client;
    HttpRequest request = fixture.request_for("/big");
    request.max_body_bytes = 2 * 1024 * 1024;
    const FetchResult result = client.fetch(request);

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::OversizedBody);
}

TEST_CASE("malformed responses are reported") {
    ServerFixture fixture;
    fixture.server.add_raw_route("/garbage", "GARBAGE NOT HTTP\r\n\r\n");

    HttpClient client;
    const FetchResult result = client.fetch(fixture.request_for("/garbage"));

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::MalformedResponse);
}

TEST_CASE("DNS failures are reported") {
    HttpClient client;
    HttpRequest request;
    request.url = *parse_url("https://does-not-exist.invalid/").url;
    const FetchResult result = client.fetch(request);

    CHECK_FALSE(result.ok);
    CHECK(result.error.kind == HttpErrorKind::Dns);
}
