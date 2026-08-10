# WebFingerprint

Evidence-based website technology fingerprinting and security-header analysis, written in C++20.

**Status: Phase 5 — JSON output + production cleanup.** The project fetches real HTTP/HTTPS targets, follows redirects (with recorded chain), enforces timeouts and size caps, parses response headers, cookies (RFC 6265 attributes, quoting, case-insensitive attribute names, malformed input), and HTML (title, meta incl. generator, scripts, stylesheets, links, inline-script detection, entity decoding, comments). An evidence model feeds a rule-table fingerprint engine with confidence scoring (OR rules accumulate via `1 - prod(1 - w)`; AND rules via `requires_all`), version extraction, per-technology minimum confidence, and case-insensitive matching. Ships with rules for nginx, Apache, Cloudflare, React, and WordPress. The `webfinger` CLI fetches a URL and prints detected technologies, optionally as JSON (`--json`, via the vendored nlohmann/json). The source is comment-free. Verified by 81 passing test cases (see Testing). This README only documents verified functionality.

## Requirements

- Windows 10/11 with Visual Studio Build Tools 2022 (C++ workload) — verified
- CMake 3.20+ (verified with the CMake bundled in Build Tools 3.31.6)
- vcpkg (for libcurl)
- Git (for vcpkg)

## One-time setup

```bat
git clone https://github.com/microsoft/vcpkg %USERPROFILE%\vcpkg
%USERPROFILE%\vcpkg\bootstrap-vcpkg.bat
```

vcpkg builds libcurl automatically on first configure (manifest mode, see `vcpkg.json`).

## Build

From the project root:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=%USERPROFILE%\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

If `cmake` is not on PATH, use the one bundled with Build Tools:

```bat
"C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
```

## Run

```bat
build\Release\webfinger.exe --version
build\Release\webfinger.exe https://example.com
build\Release\webfinger.exe https://example.com --json
```

The CLI fetches the target, follows redirects, and prints detected technologies with confidence and version. `--json` emits a machine-readable report (tool metadata, target, technologies).

## Testing

```bat
ctest --test-dir build -C Release --output-on-failure
```

81 test cases and 341 assertions: URL parsing/normalization, relative redirect resolution, header lookup, redirect chains, redirect-loop and redirect-limit handling, connection/DNS/timeout/oversize/malformed-response errors, real HTTP round-trips against a local Winsock test server (`tests/test_server.cpp`), cookie parsing (attributes, quoting, case-insensitivity, whitespace, malformed input, multi-header responses), HTML extraction (title, meta, scripts, stylesheets, links, comments, entity decoding, malformed markup, doctype skipping), engine behavior (evidence collection from real fetches and redirect chains, per-technology detection, confidence accumulation, version extraction, AND/OR rule semantics, min-confidence filtering, false-positive controls such as Tomcat not matching Apache and plain-text technology mentions not triggering detections), and JSON serialization (schema fields, round-trip through parse/dump, omission of empty values).

## Current limitations (Phase 2)

- HTTP/1.1 only. The vcpkg curl build does not include nghttp2: curl 8.21.0's `FindNGHTTP2.cmake` contains an unconditional `elseif(1)` branch that makes HTTP/2 detection fail when nghttp2 is not discoverable via pkg-config, and vcpkg does not wire pkg-config for port builds. HTTP/2 support is deferred until this is resolved.
- TLS verification is always ON; local HTTPS tests would require a locally trusted certificate and are not part of the automated suite.
- Relative redirect resolution does not collapse `..` path segments.
- The negotiated HTTP version is reported from the response; no claim of HTTP/2 support is made.

## Layout

```
include/webfingerprint/<module>/   public headers (module API)
src/<module>/                      implementation
tests/                             unit + integration tests
```

Module plan: `utils -> http -> engine -> output -> main`.
