#include "test_server.h"

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdexcept>

namespace wftest {
namespace {

void ensure_winsock() {
    static const bool initialized = [] {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("WSAStartup failed");
        }
        return true;
    }();
    (void)initialized;
}

}

TestServer::TestServer() {
    ensure_winsock();

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        throw std::runtime_error("socket() failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(s);
        throw std::runtime_error("bind() failed");
    }
    int len = sizeof(addr);
    if (getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) == SOCKET_ERROR) {
        closesocket(s);
        throw std::runtime_error("getsockname() failed");
    }
    if (listen(s, 16) == SOCKET_ERROR) {
        closesocket(s);
        throw std::runtime_error("listen() failed");
    }

    listener_ = static_cast<uintptr_t>(s);
    port_ = ntohs(addr.sin_port);
}

TestServer::~TestServer() {
    stop();
}

void TestServer::add_route(std::string path, Response response) {
    Route route;
    route.response = std::move(response);
    routes_.emplace(std::move(path), std::move(route));
}

void TestServer::add_raw_route(std::string path, std::string raw_bytes) {
    Route route;
    route.raw = std::move(raw_bytes);
    routes_.emplace(std::move(path), std::move(route));
}

void TestServer::add_delayed_route(std::string path, std::chrono::milliseconds delay, Response response) {
    Route route;
    route.response = std::move(response);
    route.delay = delay;
    route.has_delay = true;
    routes_.emplace(std::move(path), std::move(route));
}

void TestServer::start() {
    if (thread_.joinable()) {
        return;
    }
    running_ = true;
    thread_ = std::thread(&TestServer::accept_loop, this);
}

void TestServer::stop() {
    if (listener_ != 0) {
        running_ = false;
        closesocket(static_cast<SOCKET>(listener_));
        listener_ = 0;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
}

void TestServer::accept_loop() {
    while (running_) {
        SOCKET client = accept(static_cast<SOCKET>(listener_), nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        handle_connection(static_cast<uintptr_t>(client));
    }
}

void TestServer::handle_connection(uintptr_t client_socket) {
    SOCKET sock = static_cast<SOCKET>(client_socket);

    std::string request;
    char buf[4096];
    while (request.find("\r\n\r\n") == std::string::npos && request.size() < 8192) {
        const int n = recv(sock, buf, static_cast<int>(sizeof(buf)), 0);
        if (n <= 0) {
            break;
        }
        request.append(buf, static_cast<size_t>(n));
    }

    std::string path = "/";
    const size_t first_space = request.find(' ');
    if (first_space != std::string::npos) {
        const size_t second_space = request.find(' ', first_space + 1);
        const size_t end = second_space == std::string::npos ? request.size() : second_space;
        path = request.substr(first_space + 1, end - first_space - 1);
    }
    const size_t question = path.find('?');
    if (question != std::string::npos) {
        path.resize(question);
    }
    if (path.empty() || path.front() != '/') {
        path = "/";
    }

    std::string raw;
    const auto it = routes_.find(path);
    if (it != routes_.end()) {
        const Route& route = it->second;
        if (route.has_delay) {
            std::this_thread::sleep_for(route.delay);
        }
        if (!route.raw.empty()) {
            raw = route.raw;
        } else {
            const Response& r = route.response;
            raw = "HTTP/1.1 " + std::to_string(r.status) + " " + r.status_text + "\r\n";
            for (const auto& [name, value] : r.headers) {
                raw += name + ": " + value + "\r\n";
            }
            raw += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
            raw += "Connection: close\r\n\r\n";
            raw += r.body;
        }
    } else {
        raw = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    }

    size_t sent = 0;
    while (sent < raw.size()) {
        const int n = send(sock, raw.data() + sent, static_cast<int>(raw.size() - sent), 0);
        if (n <= 0) {
            break;
        }
        sent += static_cast<size_t>(n);
    }
    closesocket(sock);
}

}
