#include "webfingerprint/http/header_list.h"

#include "webfingerprint/utils/string.h"

namespace wf::http {

void HeaderList::add(std::string name, std::string value) {
    headers_.push_back(Header{std::move(name), std::move(value)});
}

bool HeaderList::contains(std::string_view name) const {
    const std::string lower = ascii_lower(name);
    for (const auto& header : headers_) {
        if (ascii_lower(header.name) == lower) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> HeaderList::get(std::string_view name) const {
    const std::string lower = ascii_lower(name);
    for (const auto& header : headers_) {
        if (ascii_lower(header.name) == lower) {
            return header.value;
        }
    }
    return std::nullopt;
}

std::vector<std::string> HeaderList::get_all(std::string_view name) const {
    const std::string lower = ascii_lower(name);
    std::vector<std::string> values;
    for (const auto& header : headers_) {
        if (ascii_lower(header.name) == lower) {
            values.push_back(header.value);
        }
    }
    return values;
}

const std::vector<Header>& HeaderList::all() const {
    return headers_;
}

size_t HeaderList::size() const {
    return headers_.size();
}

}
