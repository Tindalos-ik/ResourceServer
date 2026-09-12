#include "Base64.h"

#include <stdexcept>

namespace {
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string Base64Encode(const unsigned char* data, std::size_t length) {
    if (length == 0) {
        return {};
    }
    if (data == nullptr) {
        throw std::invalid_argument("Base64Encode data must not be null when length is non-zero");
    }

    std::string result;
    result.reserve(((length + 2) / 3) * 4);

    std::size_t index = 0;
    while (index + 3 <= length) {
        const unsigned int block = (static_cast<unsigned int>(data[index]) << 16) |
                                   (static_cast<unsigned int>(data[index + 1]) << 8) |
                                   static_cast<unsigned int>(data[index + 2]);
        result.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
        result.push_back(kBase64Alphabet[block & 0x3F]);
        index += 3;
    }

    const std::size_t remaining = length - index;
    if (remaining == 1) {
        const unsigned int block = static_cast<unsigned int>(data[index]) << 16;
        result.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
        result.append("==");
    } else if (remaining == 2) {
        const unsigned int block = (static_cast<unsigned int>(data[index]) << 16) |
                                   (static_cast<unsigned int>(data[index + 1]) << 8);
        result.push_back(kBase64Alphabet[(block >> 18) & 0x3F]);
        result.push_back(kBase64Alphabet[(block >> 12) & 0x3F]);
        result.push_back(kBase64Alphabet[(block >> 6) & 0x3F]);
        result.push_back('=');
    }

    return result;
}

std::string Base64Encode(std::string_view data) {
    return Base64Encode(reinterpret_cast<const unsigned char*>(data.data()), data.size());
}
