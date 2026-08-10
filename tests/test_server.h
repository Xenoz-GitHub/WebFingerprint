#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace wftest {

struct Response {
    int status = 200;
    std::string status_text = "OK";
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
};

struct Route {
    Response response;
    std::string raw;
    std::chrono::milliseconds delay{0};
    bool has_delay = false;
};

class TestServer {
public:
    TestServer();
    ~TestServer();

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    int port() const { return port_; }
    std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

    void add_route(std::string path, Response response);
    void add_raw_route(std::string path, std::string raw_bytes);
    void add_delayed_route(std::string path, std::chrono::milliseconds delay, Response response);

    void start();
    void stop();

private:
    void accept_loop();
    void handle_connection(uintptr_t client_socket);

    std::map<std::string, Route> routes_;
    uintptr_t listener_ = 0;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}
