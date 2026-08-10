#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wf::http {

struct Header {
    std::string name;
    std::string value;
};

class HeaderList {
public:
    void add(std::string name, std::string value);

    bool contains(std::string_view name) const;
    std::optional<std::string> get(std::string_view name) const;
    std::vector<std::string> get_all(std::string_view name) const;

    const std::vector<Header>& all() const;
    size_t size() const;

private:
    std::vector<Header> headers_;
};

}
