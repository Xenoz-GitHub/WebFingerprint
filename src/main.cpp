#include <iomanip>
#include <iostream>
#include <string_view>

#include "webfingerprint/core/version.h"
#include "webfingerprint/engine/engine.h"
#include "webfingerprint/engine/evidence.h"
#include "webfingerprint/http/http_client.h"
#include "webfingerprint/output/json.h"
#include "webfingerprint/utils/url.h"

namespace {

void print_version() {
    std::cout << wf::kProjectName << ' ' << wf::kVersionString << '\n';
}

void print_usage() {
    std::cout << "Usage: webfinger <url> [--json]\n"
              << "       webfinger --version\n";
}

void print_technologies(const std::vector<wf::engine::Technology>& technologies) {
    if (technologies.empty()) {
        std::cout << "  (none detected)\n";
        return;
    }
    std::cout << std::fixed << std::setprecision(2);
    for (const auto& technology : technologies) {
        std::cout << "  " << technology.name << "  [" << technology.category
                  << "]  confidence=" << technology.confidence;
        if (!technology.version.empty()) {
            std::cout << "  version=" << technology.version;
        }
        std::cout << '\n';
        for (const auto& evidence : technology.evidence) {
            std::cout << "    - " << evidence << '\n';
        }
    }
}

}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage();
        return 2;
    }
    const std::string_view argument = argv[1];
    if (argument == "--version") {
        print_version();
        return 0;
    }
    if (argument == "--help" || argument == "-h") {
        print_usage();
        return 0;
    }
    bool json_output = false;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--json") {
            json_output = true;
        }
    }

    const auto parsed = wf::parse_url(argument);
    if (!parsed.url) {
        std::cerr << "Invalid URL: " << parsed.error << '\n';
        return 2;
    }

    wf::http::HttpClient client;
    wf::http::HttpRequest request;
    request.url = *parsed.url;
    const auto result = client.fetch(request);

    if (!result.ok) {
        std::cerr << "Fetch failed: " << result.error.detail << '\n';
        return 1;
    }

    const auto evidence = wf::engine::collect_evidence(result);
    wf::engine::FingerprintEngine engine;
    const auto technologies = engine.analyze(evidence);

    if (json_output) {
        std::cout << wf::output::scan_to_json(argument, evidence, technologies).dump(2) << '\n';
        return 0;
    }

    std::cout << wf::kProjectName << ' ' << wf::kVersionString << '\n';
    std::cout << "Target:  " << evidence.url << '\n';
    std::cout << "Status:  " << evidence.status_code << '\n';
    std::cout << "Detected technologies:\n";
    print_technologies(technologies);
    return 0;
}
