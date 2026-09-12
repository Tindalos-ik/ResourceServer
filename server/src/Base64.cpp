#include "Base64.h"

#include <stdexcept>

namespace {
constexpr char kBase64Alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int Base64Value(unsigned char character) {
    if (character >= 'A' && character <= 'Z') {
        return character - 'A';
    }
    if (character >= 'a' && character <= 'z') {
        return character - 'a' + 26;
    }
    if (character >= '0' && character <= '9') {
        return character - '0' + 52;
    }
    if (character == '+') {
        return 62;
    }
    if (character == '/') {
        return 63;
    }
    return -1;
}
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

bool Base64Decode(std::string_view encoded, std::string& decoded) {
    // Base64 每四个字符对应三个原始字节，长度不是 4 的倍数说明数据不完整。
    if (encoded.size() % 4 != 0) {
        return false;
    }

    std::string result;
    result.reserve((encoded.size() / 4) * 3);

    for (std::size_t index = 0; index < encoded.size(); index += 4) {
        const unsigned char char0 = static_cast<unsigned char>(encoded[index]);
        const unsigned char char1 = static_cast<unsigned char>(encoded[index + 1]);
        const unsigned char char2 = static_cast<unsigned char>(encoded[index + 2]);
        const unsigned char char3 = static_cast<unsigned char>(encoded[index + 3]);

        const int value0 = Base64Value(char0);
        const int value1 = Base64Value(char1);
        const bool padding2 = char2 == '=';
        const bool padding3 = char3 == '=';
        const int value2 = padding2 ? 0 : Base64Value(char2);
        const int value3 = padding3 ? 0 : Base64Value(char3);

        // 前两个字符不能是补位；补位只允许出现在最后一组，且 "=" 不能单独位于第三位。
        if (value0 < 0 || value1 < 0 || value2 < 0 || value3 < 0 ||
            (padding2 && !padding3) || ((padding2 || padding3) && index + 4 != encoded.size())) {
            return false;
        }

        const unsigned int block = (static_cast<unsigned int>(value0) << 18) |
                                   (static_cast<unsigned int>(value1) << 12) |
                                   (static_cast<unsigned int>(value2) << 6) |
                                   static_cast<unsigned int>(value3);
        result.push_back(static_cast<char>((block >> 16) & 0xFF));
        if (!padding2) {
            result.push_back(static_cast<char>((block >> 8) & 0xFF));
        }
        if (!padding3) {
            result.push_back(static_cast<char>(block & 0xFF));
        }
    }

    decoded = std::move(result);
    return true;
}
