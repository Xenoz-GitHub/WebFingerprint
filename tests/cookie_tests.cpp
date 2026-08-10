#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "webfingerprint/http/cookie.h"
#include "webfingerprint/http/header_list.h"
#include "webfingerprint/http/http_client.h"
#include "webfingerprint/utils/url.h"
#include "test_server.h"

using wf::http::Cookie;
using wf::http::HeaderList;
using wf::http::HttpClient;
using wf::http::parse_cookies;
using wf::http::parse_set_cookie;

TEST_CASE("a simple cookie is parsed") {
    const auto cookie = parse_set_cookie("session=abc123");
    REQUIRE(cookie.has_value());
    CHECK(cookie->name == "session");
    CHECK(cookie->value == "abc123");
    CHECK_FALSE(cookie->secure);
    CHECK_FALSE(cookie->http_only);
}

TEST_CASE("all standard attributes are extracted") {
    const auto cookie = parse_set_cookie(
        "session=abc123; Path=/; Domain=example.com; Max-Age=3600; "
        "Expires=Wed, 21 Oct 2026 07:28:00 GMT; Secure; HttpOnly; SameSite=Lax");
    REQUIRE(cookie.has_value());
    CHECK(cookie->path == "/");
    CHECK(cookie->domain == "example.com");
    CHECK(cookie->max_age == "3600");
    CHECK(cookie->expires == "Wed, 21 Oct 2026 07:28:00 GMT");
    CHECK(cookie->secure);
    CHECK(cookie->http_only);
    CHECK(cookie->same_site == "Lax");
}

TEST_CASE("attribute names are case-insensitive") {
    const auto cookie = parse_set_cookie("a=1; SECURE; httponly; SAMESITE=Strict; Max-Age=60; samesite=Strict");
    REQUIRE(cookie.has_value());
    CHECK(cookie->secure);
    CHECK(cookie->http_only);
    CHECK(cookie->same_site == "Strict");
    CHECK(cookie->max_age == "60");
}

TEST_CASE("quoted values are unquoted") {
    const auto cookie = parse_set_cookie("name=\"value with spaces\"");
    REQUIRE(cookie.has_value());
    CHECK(cookie->name == "name");
    CHECK(cookie->value == "value with spaces");
}

TEST_CASE("empty cookie names are tolerated") {
    const auto cookie = parse_set_cookie("=value");
    REQUIRE(cookie.has_value());
    CHECK(cookie->name.empty());
    CHECK(cookie->value == "value");
}

TEST_CASE("malformed cookies are rejected") {
    CHECK_FALSE(parse_set_cookie("").has_value());
    CHECK_FALSE(parse_set_cookie("justtext").has_value());
    CHECK_FALSE(parse_set_cookie("; Secure").has_value());
    CHECK_FALSE(parse_set_cookie("bad name=1").has_value());
}

TEST_CASE("values may contain equals signs; attributes do not split on them") {
    const auto cookie = parse_set_cookie("a=b=c; Path=/x");
    REQUIRE(cookie.has_value());
    CHECK(cookie->value == "b=c");
    CHECK(cookie->path == "/x");
}

TEST_CASE("surrounding whitespace is tolerated") {
    const auto cookie = parse_set_cookie("  session = abc123 ; Secure ");
    REQUIRE(cookie.has_value());
    CHECK(cookie->name == "session");
    CHECK(cookie->value == "abc123");
    CHECK(cookie->secure);
}

TEST_CASE("unknown attributes are ignored") {
    const auto cookie = parse_set_cookie("a=1; Priority=High; SameParty=1");
    REQUIRE(cookie.has_value());
    CHECK(cookie->value == "1");
}

TEST_CASE("multiple Set-Cookie headers produce multiple cookies") {
    HeaderList headers;
    headers.add("Set-Cookie", "a=1; HttpOnly");
    headers.add("Set-Cookie", "b=2; Secure");
    headers.add("Other", "x");

    const auto cookies = parse_cookies(headers);
    REQUIRE(cookies.size() == 2);
    CHECK(cookies[0].name == "a");
    CHECK(cookies[0].http_only);
    CHECK_FALSE(cookies[0].secure);
    CHECK(cookies[1].name == "b");
    CHECK(cookies[1].secure);
}

TEST_CASE("cookies are extracted from a real response") {
    wftest::TestServer server;
    server.add_route("/cookies",
                     {200, "OK",
                      {{"Set-Cookie", "session=abc123; Path=/; HttpOnly; Secure; SameSite=Lax"},
                       {"Set-Cookie", "tracking=1; Max-Age=86400"}},
                      "body"});
    server.start();

    HttpClient client;
    wf::http::HttpRequest request;
    request.url = *wf::parse_url(server.base_url() + "/cookies").url;
    const auto result = client.fetch(request);
    server.stop();

    REQUIRE(result.ok);
    const auto cookies = parse_cookies(result.response.headers);
    REQUIRE(cookies.size() == 2);
    CHECK(cookies[0].name == "session");
    CHECK(cookies[0].value == "abc123");
    CHECK(cookies[0].http_only);
    CHECK(cookies[0].secure);
    CHECK(cookies[0].same_site == "Lax");
    CHECK(cookies[0].path == "/");
    CHECK(cookies[1].name == "tracking");
    CHECK(cookies[1].max_age == "86400");
}
