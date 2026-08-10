#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "webfingerprint/http/header_list.h"
#include "webfingerprint/utils/url.h"

namespace wf::http {

enum class HttpVersion {
    Unknown,
    Http10,
    Http11,
    Http2,
    Http3,
};

struct HttpRequest {
    Url url;
    std::string user_agent = "WebFingerprint/0.1 (educational security research)";
    std::chrono::seconds connect_timeout{10};
    std::chrono::seconds total_timeout{30};
    int max_redirects = 5;
    size_t max_body_bytes = 2 * 1024 * 1024;
};

struct HttpResponse {
    int status_code = 0;
    std::string status_text;
    HttpVersion version = HttpVersion::Unknown;
    HeaderList headers;
    std::string body;
    Url request_url;
    std::vector<std::string> redirect_chain;
};

enum class HttpErrorKind {
    Dns,
    Connection,
    Tls,
    Timeout,
    RedirectLoop,
    TooManyRedirects,
    OversizedBody,
    MalformedResponse,
    Generic,
};

struct HttpError {
    HttpErrorKind kind;
    std::string detail;
};

struct FetchResult {
    bool ok = false;
    HttpResponse response;
    HttpError error;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    FetchResult fetch(const HttpRequest& request) const;

private:
    FetchResult fetch_once(const HttpRequest& request, const Url& url) const;
};

}
