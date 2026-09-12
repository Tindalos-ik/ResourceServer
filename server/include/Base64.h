#pragma once

#include <cstddef>
#include <string>
#include <string_view>

// 将任意二进制数据编码为 Base64 文本。返回结果不包含换行符。
std::string Base64Encode(const unsigned char* data, std::size_t length);

// 便捷重载：可直接传入 std::string（其中允许包含 '\0'）。
std::string Base64Encode(std::string_view data);

// 将 Base64 文本还原为原始二进制数据；输入不合法时返回 false。
bool Base64Decode(std::string_view encoded, std::string& decoded);
