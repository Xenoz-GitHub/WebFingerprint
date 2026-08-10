#include "webfingerprint/http/http_client.h"

#include <curl/curl.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

#include "webfingerprint/utils/string.h"

namespace wf::http {
namespace {

std::atomic<bool> g_curl_initialized{false};

struct CurlGlobal {
    CurlGlobal() {
        if (!g_curl_initialized.exchange(true)) {
            curl_global_init(CURL_GLOBAL_ALL);
        }
    }
};

struct TransferContext {
    std::string body;
    HeaderList headers;
    std::string status_line;
    size_t max_body_bytes = 0;
    bool oversized = false;
};

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* context = static_cast<TransferContext*>(userdata);
    const size_t bytes = size * nmemb;
    if (context->max_body_bytes > 0 && context->body.size() + bytes > context->max_body_bytes) {
        context->oversized = true;
        return 0;
    }
    context->body.append(ptr, bytes);
    return bytes;
}

size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* context = static_cast<TransferContext*>(userdata);
    const size_t bytes = size * nitems;
    const std::string_view line(buffer, bytes);

    if (wf::starts_with_ci(line, "HTTP/")) {
        context->status_line = std::string(line);
        return bytes;
    }

    const size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return bytes;
    }
    const std::string name = wf::trim(line.substr(0, colon));
    const std::string value = wf::trim(line.substr(colon + 1));
    if (!name.empty()) {
        context->headers.add(std::move(name), std::move(value));
    }
    return bytes;
}

HttpVersion parse_http_version(std::string_view status_line) {
    if (status_line.size() < 9) {
        return HttpVersion::Unknown;
    }
    if (starts_with_ci(status_line, "HTTP/2")) {
        return HttpVersion::Http2;
    }
    if (starts_with_ci(status_line, "HTTP/3")) {
        return HttpVersion::Http3;
    }
    if (starts_with_ci(status_line, "HTTP/1.0")) {
        return HttpVersion::Http10;
    }
    if (starts_with_ci(status_line, "HTTP/1.1")) {
        return HttpVersion::Http11;
    }
    return HttpVersion::Unknown;
}

int parse_status_code(std::string_view status_line) {
    const size_t first_space = status_line.find(' ');
    if (first_space == std::string_view::npos) {
        return 0;
    }
    const size_t code_start = first_space + 1;
    const size_t code_end = status_line.find(' ', code_start);
    const std::string_view code_text =
        code_end == std::string_view::npos ? status_line.substr(code_start)
                                           : status_line.substr(code_start, code_end - code_start);
    if (code_text.empty()) {
        return 0;
    }
    int code = 0;
    for (char c : code_text) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return 0;
        }
        code = code * 10 + (c - '0');
    }
    return code;
}

std::string parse_status_text(std::string_view status_line) {
    const size_t first_space = status_line.find(' ');
    if (first_space == std::string_view::npos) {
        return {};
    }
    const size_t second_space = status_line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) {
        return {};
    }
    return wf::trim(status_line.substr(second_space + 1));
}

HttpErrorKind map_curl_error(CURLcode code, const TransferContext& context) {
    if (context.oversized) {
        return HttpErrorKind::OversizedBody;
    }
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
            return HttpErrorKind::Dns;
        case CURLE_COULDNT_CONNECT:
            return HttpErrorKind::Connection;
        case CURLE_OPERATION_TIMEDOUT:
            return HttpErrorKind::Timeout;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CACERT_BADFILE:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
        case CURLE_SSL_CRL_BADFILE:
        case CURLE_SSL_ISSUER_ERROR:
        case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
        case CURLE_SSL_INVALIDCERTSTATUS:
            return HttpErrorKind::Tls;
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
        case CURLE_WEIRD_SERVER_REPLY:
        case CURLE_HTTP2:
        case CURLE_UNSUPPORTED_PROTOCOL:
            return HttpErrorKind::MalformedResponse;
        case CURLE_TOO_MANY_REDIRECTS:
            return HttpErrorKind::TooManyRedirects;
        default:
            return HttpErrorKind::Generic;
    }
}

}

HttpClient::HttpClient() {
    static CurlGlobal global;
    (void)global;
}

HttpClient::~HttpClient() = default;

FetchResult HttpClient::fetch(const HttpRequest& request) const {
    FetchResult result;
    std::vector<std::string> chain;
    Url current = request.url;
    chain.push_back(current.to_string());

    for (;;) {
        FetchResult hop = fetch_once(request, current);
        if (!hop.ok) {
            result.ok = false;
            result.error = hop.error;
            result.response.redirect_chain = chain;
            return result;
        }

        result.ok = true;
        result.response = hop.response;
        result.response.redirect_chain = chain;

        const bool is_redirect = result.response.status_code >= 300 &&
                                 result.response.status_code < 400;
        if (!is_redirect) {
            return result;
        }
        const auto location = result.response.headers.get("Location");
        if (!location) {
            return result;
        }

        const auto next = resolve_relative(current, *location);
        if (!next) {
            return result;
        }

        const std::string next_string = next->to_string();

        if (std::find(chain.begin(), chain.end(), next_string) != chain.end()) {
            result.ok = false;
            result.error = {HttpErrorKind::RedirectLoop, "redirect loop: " + next_string};
            return result;
        }

        if (chain.size() >= static_cast<size_t>(request.max_redirects)) {
            result.ok = false;
            result.error = {HttpErrorKind::TooManyRedirects,
                            "too many redirects (limit " + std::to_string(request.max_redirects) +
                                ")"};
            return result;
        }

        current = *next;
        chain.push_back(next_string);
    }
}

FetchResult HttpClient::fetch_once(const HttpRequest& request, const Url& url) const {
    FetchResult result;
    TransferContext context;
    context.max_body_bytes = request.max_body_bytes;

    CURL* curl = curl_easy_init();
    if (!curl) {
        result.error = {HttpErrorKind::Generic, "failed to create curl handle"};
        return result;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.to_string().c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, request.user_agent.c_str());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                     static_cast<long>(request.connect_timeout.count()));
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(request.total_timeout.count()));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 0L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &context);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_callback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &context);

    const CURLcode code = curl_easy_perform(curl);
    const char* error_text = curl_easy_strerror(code);

    if (code == CURLE_OK) {
        result.ok = true;
        result.response.status_code = parse_status_code(context.status_line);
        result.response.status_text = parse_status_text(context.status_line);
        result.response.version = parse_http_version(context.status_line);
        result.response.headers = std::move(context.headers);
        result.response.body = std::move(context.body);
        result.response.request_url = url;
    } else {
        result.ok = false;
        result.error.kind = map_curl_error(code, context);
        result.error.detail = error_text ? error_text : "curl error";
    }

    curl_easy_cleanup(curl);
    return result;
}

}
