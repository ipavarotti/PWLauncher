#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace el {

namespace base64 {

std::string Encode(const unsigned char* data, size_t len);
std::string Encode(const std::string& data);
std::vector<unsigned char> Decode(const std::string& text);

bool IsCode(char c);

}

std::wstring DecodeFileName(const std::string& coded, std::string& outPrefix);
std::wstring DecodeFileName(const std::string& coded);

}
