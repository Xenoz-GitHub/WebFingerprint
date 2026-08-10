#include <catch2/catch_test_macros.hpp>

#include <string>

#include "webfingerprint/utils/url.h"

using wf::parse_url;
using wf::resolve_relative;

TEST_CASE("bare host defaults to https with root path") {
    const auto result = parse_url("example.com");
    REQUIRE(result.url.has_value());
    CHECK(result.url->scheme == "https");
    CHECK(result.url->host == "example.com");
    CHECK(result.url->path == "/");
    CHECK_FALSE(result.url->port.has_value());
    CHECK(result.url->to_string() == "https://example.com/");
}

TEST_CASE("explicit schemes are honored") {
    CHECK(parse_url("http://example.com").url->scheme == "http");
    CHECK(parse_url("https://example.com").url->scheme == "https");
}

TEST_CASE("scheme and host are lowercased") {
    const auto result = parse_url("HTTP://Example.COM/Foo");
    REQUIRE(result.url.has_value());
    CHECK(result.url->scheme == "http");
    CHECK(result.url->host == "example.com");
    CHECK(result.url->path == "/Foo");
    CHECK(result.url->to_string() == "http://example.com/Foo");
}

TEST_CASE("paths, queries, and fragments are separated") {
    const auto result = parse_url("https://example.com/a/b?x=1&y=2#section");
    REQUIRE(result.url.has_value());
    CHECK(result.url->path == "/a/b");
    CHECK(result.url->query == "x=1&y=2");
    CHECK(result.url->fragment == "section");
    CHECK(result.url->to_string() == "https://example.com/a/b?x=1&y=2#section");
}

TEST_CASE("explicit default port is parsed but omitted in canonical form") {
    const auto result = parse_url("http://example.com:80/");
    REQUIRE(result.url.has_value());
    REQUIRE(result.url->port.has_value());
    CHECK(*result.url->port == 80);
    CHECK(result.url->to_string() == "http://example.com/");
}

TEST_CASE("non-default port is preserved") {
    const auto result = parse_url("example.com:8080/x");
    REQUIRE(result.url.has_value());
    REQUIRE(result.url->port.has_value());
    CHECK(*result.url->port == 8080);
    CHECK(result.url->to_string() == "https://example.com:8080/x");
}

TEST_CASE("invalid ports are rejected") {
    CHECK_FALSE(parse_url("example.com:0").url.has_value());
    CHECK_FALSE(parse_url("example.com:99999").url.has_value());
    CHECK_FALSE(parse_url("example.com:abc").url.has_value());
    CHECK_FALSE(parse_url("example.com:").url.has_value());
}

TEST_CASE("invalid inputs are rejected with a reason") {
    CHECK(parse_url("").error == "empty target");
    CHECK(parse_url("   ").error == "empty target");
    CHECK_FALSE(parse_url("https://").url.has_value());
    CHECK_FALSE(parse_url("http:///path").url.has_value());
    CHECK_FALSE(parse_url("ftp://example.com").url.has_value());
    CHECK_FALSE(parse_url("exa mple.com").url.has_value());
}

TEST_CASE("URLs with embedded credentials are rejected") {
    const auto result = parse_url("https://user:pass@example.com/");
    CHECK_FALSE(result.url.has_value());
    CHECK(result.error.find("credentials") != std::string::npos);
}

TEST_CASE("bracketed IPv6 hosts are supported") {
    const auto result = parse_url("http://[::1]:8080/x");
    REQUIRE(result.url.has_value());
    CHECK(result.url->host == "::1");
    REQUIRE(result.url->port.has_value());
    CHECK(*result.url->port == 8080);
    CHECK(result.url->to_string() == "http://[::1]:8080/x");
}

TEST_CASE("unbracketed IPv6 is rejected with a hint") {
    const auto result = parse_url("http://::1/x");
    CHECK_FALSE(result.url.has_value());
}

TEST_CASE("normalization is idempotent") {
    const std::vector<std::string> inputs = {
        "example.com",
        "https://example.com/path?q=1#frag",
        "http://Example.COM:8080/a",
    };
    for (const std::string& input : inputs) {
        const auto first = parse_url(input);
        REQUIRE(first.url.has_value());
        const auto second = parse_url(first.url->to_string());
        REQUIRE(second.url.has_value());
        CHECK(second.url->to_string() == first.url->to_string());
    }
}

TEST_CASE("resolve_relative handles absolute URLs") {
    const auto base = parse_url("https://example.com/docs/page");
    REQUIRE(base.url.has_value());

    const auto absolute = resolve_relative(*base.url, "http://other.com/x");
    REQUIRE(absolute.has_value());
    CHECK(absolute->to_string() == "http://other.com/x");

    const auto same_scheme = resolve_relative(*base.url, "https://other.com/");
    REQUIRE(same_scheme.has_value());
    CHECK(same_scheme->to_string() == "https://other.com/");
}

TEST_CASE("resolve_relative rejects unsupported or malformed locations") {
    const auto base = parse_url("https://example.com/docs/page");
    REQUIRE(base.url.has_value());
    CHECK_FALSE(resolve_relative(*base.url, "ftp://example.com/x").has_value());
    CHECK_FALSE(resolve_relative(*base.url, "http://").has_value());
    CHECK_FALSE(resolve_relative(*base.url, "#fragment").has_value());
}

TEST_CASE("resolve_relative handles protocol-relative and root-absolute locations") {
    const auto base = parse_url("https://example.com/docs/page");
    REQUIRE(base.url.has_value());

    const auto protocol_relative = resolve_relative(*base.url, "//cdn.example.com/app.js");
    REQUIRE(protocol_relative.has_value());
    CHECK(protocol_relative->to_string() == "https://cdn.example.com/app.js");

    const auto root = resolve_relative(*base.url, "/blog");
    REQUIRE(root.has_value());
    CHECK(root->to_string() == "https://example.com/blog");

    const auto root_with_query = resolve_relative(*base.url, "/blog?x=1");
    REQUIRE(root_with_query.has_value());
    CHECK(root_with_query->to_string() == "https://example.com/blog?x=1");
}

TEST_CASE("resolve_relative handles query-only and relative-path locations") {
    const auto base = parse_url("https://example.com/docs/page");
    REQUIRE(base.url.has_value());

    const auto query = resolve_relative(*base.url, "?x=1");
    REQUIRE(query.has_value());
    CHECK(query->to_string() == "https://example.com/docs/page?x=1");

    const auto relative = resolve_relative(*base.url, "sub");
    REQUIRE(relative.has_value());
    CHECK(relative->to_string() == "https://example.com/docs/sub");
}

TEST_CASE("resolve_relative resolves against the root when the path is at the root") {
    const auto base = parse_url("https://example.com/");
    REQUIRE(base.url.has_value());

    const auto relative = resolve_relative(*base.url, "sub");
    REQUIRE(relative.has_value());
    CHECK(relative->to_string() == "https://example.com/sub");
}

TEST_CASE("resolve_relative preserves the base port") {
    const auto base = parse_url("https://example.com:8443/docs/page");
    REQUIRE(base.url.has_value());

    const auto absolute = resolve_relative(*base.url, "/x");
    REQUIRE(absolute.has_value());
    CHECK(absolute->to_string() == "https://example.com:8443/x");
}
