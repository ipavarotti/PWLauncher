#pragma once
#include <windows.h>
#include <string>
#include <cstdint>
#include <vector>

namespace el {

class Md5
{
public:
	Md5();
	void Reset();
	void Update(const void* data, size_t len);
	void Final(unsigned char digest[16]);
	std::string HexDigest();

	static std::string Buffer(const void* data, size_t len);
	static bool File(const std::wstring& path, std::string& hexOut);
	static bool StreamRange(HANDLE h, std::string& hexOut);

private:
	void Transform(const unsigned char block[64]);
	uint32_t m_state[4];
	uint64_t m_count;
	unsigned char m_buffer[64];
};

}
