#include <catch2/catch_test_macros.hpp>

#include <string>

#include "webfingerprint/utils/html.h"

using wf::utils::analyze_html;
using wf::utils::HtmlInfo;

TEST_CASE("empty and tagless input produce an empty result") {
    const HtmlInfo info = analyze_html("");
    CHECK(info.title.empty());
    CHECK(info.meta.empty());
    CHECK(info.scripts.empty());
    CHECK(info.stylesheets.empty());
    CHECK(info.links.empty());
    CHECK_FALSE(info.has_inline_script);

    const HtmlInfo plain = analyze_html("just plain text, no tags at all");
    CHECK(plain.scripts.empty());
    CHECK(plain.links.empty());
}

TEST_CASE("the title is extracted and entities are decoded") {
    const HtmlInfo info = analyze_html("<html><head><title>My Page &amp; More</title></head></html>");
    CHECK(info.title == "My Page & More");
}

TEST_CASE("scripts with src attributes are collected") {
    const HtmlInfo info = analyze_html(
        "<html><head>"
        "<script src=\"/js/app.js\"></script>"
        "<script src=\"https://cdn.example.com/lib.js\" async></script>"
        "<script src='//cdn.example.com/proto.js'></script>"
        "</head></html>");
    REQUIRE(info.scripts.size() == 3);
    CHECK(info.scripts[0] == "/js/app.js");
    CHECK(info.scripts[1] == "https://cdn.example.com/lib.js");
    CHECK(info.scripts[2] == "//cdn.example.com/proto.js");
}

TEST_CASE("inline scripts are flagged only when no src is present") {
    const HtmlInfo inline_only = analyze_html("<body><script>var x = 1;</script></body>");
    CHECK(inline_only.has_inline_script);

    const HtmlInfo with_src = analyze_html("<script src='/x.js'>if (a > b) {}</script>");
    CHECK_FALSE(with_src.has_inline_script);
}

TEST_CASE("stylesheet links are collected") {
    const HtmlInfo info = analyze_html(
        "<link rel=\"stylesheet\" href=\"/assets/main.css\">"
        "<link rel='preload stylesheet' href=\"/assets/extra.css\">"
        "<link rel=\"icon\" href=\"/favicon.ico\">");
    REQUIRE(info.stylesheets.size() == 2);
    CHECK(info.stylesheets[0] == "/assets/main.css");
    CHECK(info.stylesheets[1] == "/assets/extra.css");
}

TEST_CASE("meta tags are captured with meaningful names") {
    const HtmlInfo info = analyze_html(
        "<meta charset=\"utf-8\">"
        "<meta name=\"description\" content=\"A test page\">"
        "<meta name=\"generator\" content=\"WordPress 6.5\">"
        "<meta http-equiv=\"refresh\" content=\"0; url=/new\">"
        "<meta property=\"og:title\" content=\"Hello\">");
    REQUIRE(info.meta.size() == 5);
    CHECK(info.meta[0].name == "charset");
    CHECK(info.meta[0].content == "utf-8");
    CHECK(info.meta[1].name == "description");
    CHECK(info.meta[1].content == "A test page");
    CHECK(info.meta[2].name == "generator");
    CHECK(info.meta[2].content == "WordPress 6.5");
    CHECK(info.meta[3].name == "http-equiv");
    CHECK(info.meta[3].content == "0; url=/new");
    CHECK(info.meta[4].name == "og:title");
    CHECK(info.meta[4].content == "Hello");
}

TEST_CASE("anchor links are collected") {
    const HtmlInfo info = analyze_html(
        "<a href='/page1'>one</a>"
        "<a href=\"https://other.com/x?y=1\">two</a>"
        "<a>no href</a>");
    REQUIRE(info.links.size() == 2);
    CHECK(info.links[0] == "/page1");
    CHECK(info.links[1] == "https://other.com/x?y=1");
}

TEST_CASE("comments are ignored") {
    const HtmlInfo info = analyze_html(
        "<!-- <script src='/fake.js'></script> -->"
        "<script src='/real.js'></script>");
    REQUIRE(info.scripts.size() == 1);
    CHECK(info.scripts[0] == "/real.js");
}

TEST_CASE("script content is not parsed as markup") {
    const HtmlInfo info = analyze_html(
        "<script>var s = \"<a href='/not-a-link'>x</a>\";</script>"
        "<a href='/real'>r</a>");
    REQUIRE(info.links.size() == 1);
    CHECK(info.links[0] == "/real");
}

TEST_CASE("style content is skipped") {
    const HtmlInfo info = analyze_html("<style>a { color: red }</style><link rel='stylesheet' href='/x.css'>");
    REQUIRE(info.stylesheets.size() == 1);
    CHECK(info.stylesheets[0] == "/x.css");
}

TEST_CASE("entities in hrefs are decoded") {
    const HtmlInfo info = analyze_html("<a href='/search?a=1&amp;b=2'>x</a>");
    REQUIRE(info.links.size() == 1);
    CHECK(info.links[0] == "/search?a=1&b=2");
}

TEST_CASE("uppercase tags and attributes are handled") {
    const HtmlInfo info = analyze_html(
        "<SCRIPT SRC=\"/U.JS\"></SCRIPT>"
        "<LINK REL=\"STYLESHEET\" HREF=\"/u.css\">");
    REQUIRE(info.scripts.size() == 1);
    CHECK(info.scripts[0] == "/U.JS");
    REQUIRE(info.stylesheets.size() == 1);
    CHECK(info.stylesheets[0] == "/u.css");
}

TEST_CASE("malformed markup degrades gracefully") {
    const HtmlInfo unclosed_script = analyze_html("<script src='/a.js'<link href='/b.css' rel='stylesheet'");
    REQUIRE(unclosed_script.scripts.size() == 1);
    CHECK(unclosed_script.scripts[0] == "/a.js");

    const HtmlInfo unclosed_meta = analyze_html("<meta name='generator' content='unterminated");
    REQUIRE(unclosed_meta.meta.size() == 1);
    CHECK(unclosed_meta.meta[0].name == "generator");

    const HtmlInfo mixed = analyze_html("<a href='/ok'>x</a><div class='unterminated");
    REQUIRE(mixed.links.size() == 1);
    CHECK(mixed.links[0] == "/ok");
}

TEST_CASE("doctype and processing instructions are skipped") {
    const HtmlInfo info = analyze_html("<!DOCTYPE html><link rel='stylesheet' href='/s.css'>");
    REQUIRE(info.stylesheets.size() == 1);
    CHECK(info.stylesheets[0] == "/s.css");
}
