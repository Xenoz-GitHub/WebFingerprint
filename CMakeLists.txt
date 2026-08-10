cmake_minimum_required(VERSION 3.20)
project(WebFingerprint VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(CURL REQUIRED)

include(FetchContent)
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

if(MSVC)
    add_compile_options(/W4)
endif()

add_library(wf_utils STATIC
    src/utils/string.cpp
    src/utils/url.cpp
    src/utils/html.cpp
)
target_include_directories(wf_utils PUBLIC include)

add_library(wf_http STATIC
    src/http/header_list.cpp
    src/http/http_client.cpp
    src/http/cookie.cpp
)
target_include_directories(wf_http PUBLIC include)
target_link_libraries(wf_http PUBLIC wf_utils CURL::libcurl)

add_library(wf_engine STATIC
    src/engine/evidence.cpp
    src/engine/engine.cpp
    src/engine/technologies.cpp
)
target_include_directories(wf_engine PUBLIC include)
target_link_libraries(wf_engine PUBLIC wf_http)

add_library(wf_output STATIC
    src/output/json.cpp
)
target_include_directories(wf_output PUBLIC include)
target_link_libraries(wf_output PUBLIC wf_engine nlohmann_json::nlohmann_json)

add_executable(webfinger src/main.cpp)
target_link_libraries(webfinger PRIVATE wf_output)

option(WF_BUILD_TESTS "Build the test suite" ON)
if(WF_BUILD_TESTS)
    enable_testing()
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(wf_tests
        tests/url_tests.cpp
        tests/http_tests.cpp
        tests/cookie_tests.cpp
        tests/html_tests.cpp
        tests/engine_tests.cpp
        tests/output_tests.cpp
        tests/test_server.cpp
    )
    target_include_directories(wf_tests PRIVATE include tests)
    target_link_libraries(wf_tests PRIVATE wf_output Catch2::Catch2WithMain)
    if(WIN32)
        target_link_libraries(wf_tests PRIVATE ws2_32)
    endif()
    add_test(NAME wf_tests COMMAND wf_tests)
endif()
