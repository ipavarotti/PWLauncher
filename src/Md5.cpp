#include "Md5.h"
#include "Common.h"
#include <cstring>
#include <cstdio>

namespace el {

static const uint32_t S[64] = {
	7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
	5, 9,14,20, 5, 9,14,20, 5, 9,14,20, 5, 9,14,20,
	4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
	6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21
};

static const uint32_t K[64] = {
	0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,
	0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
	0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,
	0x6b901122,0xfd987193,0xa679438e,0x49b40821,
	0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,
	0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
	0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,
	0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
	0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,
	0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
	0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,
	0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
	0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,
	0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
	0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,
	0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
};

static inline uint32_t Rotl(uint32_t x, uint32_t c) { return (x << c) | (x >> (32 - c)); }

Md5::Md5() { Reset(); }

void Md5::Reset()
{
	m_state[0] = 0x67452301;
	m_state[1] = 0xefcdab89;
	m_state[2] = 0x98badcfe;
	m_state[3] = 0x10325476;
	m_count = 0;
	memset(m_buffer, 0, sizeof(m_buffer));
}

void Md5::Transform(const unsigned char block[64])
{
	uint32_t M[16];
	for (int i = 0; i < 16; ++i)
		M[i] = (uint32_t)block[i * 4] | ((uint32_t)block[i * 4 + 1] << 8) |
		       ((uint32_t)block[i * 4 + 2] << 16) | ((uint32_t)block[i * 4 + 3] << 24);

	uint32_t A = m_state[0], B = m_state[1], C = m_state[2], D = m_state[3];

	for (uint32_t i = 0; i < 64; ++i)
	{
		uint32_t F; uint32_t g;
		if (i < 16)      { F = (B & C) | (~B & D);        g = i; }
		else if (i < 32) { F = (D & B) | (~D & C);        g = (5 * i + 1) % 16; }
		else if (i < 48) { F = B ^ C ^ D;                 g = (3 * i + 5) % 16; }
		else             { F = C ^ (B | ~D);              g = (7 * i) % 16; }
		F = F + A + K[i] + M[g];
		A = D; D = C; C = B;
		B = B + Rotl(F, S[i]);
	}
	m_state[0] += A; m_state[1] += B; m_state[2] += C; m_state[3] += D;
}

void Md5::Update(const void* data, size_t len)
{
	const unsigned char* p = (const unsigned char*)data;
	uint64_t index = m_count & 0x3F;
	m_count += len;
	size_t partLen = 64 - (size_t)index;
	size_t i = 0;

	if (len >= partLen)
	{
		memcpy(&m_buffer[index], p, partLen);
		Transform(m_buffer);
		for (i = partLen; i + 63 < len; i += 64)
			Transform(&p[i]);
		index = 0;
	}
	memcpy(&m_buffer[index], &p[i], len - i);
}

void Md5::Final(unsigned char digest[16])
{
	static const unsigned char PADDING[64] = { 0x80 };
	uint64_t bits = m_count * 8;
	unsigned char lenBytes[8];
	for (int i = 0; i < 8; ++i) lenBytes[i] = (unsigned char)((bits >> (8 * i)) & 0xFF);

	size_t index = (size_t)(m_count & 0x3F);
	size_t padLen = (index < 56) ? (56 - index) : (120 - index);
	Update(PADDING, padLen);
	Update(lenBytes, 8);

	for (int i = 0; i < 4; ++i)
	{
		digest[i * 4]     = (unsigned char)(m_state[i] & 0xFF);
		digest[i * 4 + 1] = (unsigned char)((m_state[i] >> 8) & 0xFF);
		digest[i * 4 + 2] = (unsigned char)((m_state[i] >> 16) & 0xFF);
		digest[i * 4 + 3] = (unsigned char)((m_state[i] >> 24) & 0xFF);
	}
}

std::string Md5::HexDigest()
{
	unsigned char d[16];
	Final(d);
	char hex[33];
	for (int i = 0; i < 16; ++i) sprintf(hex + i * 2, "%02x", d[i]);
	hex[32] = 0;
	return std::string(hex);
}

std::string Md5::Buffer(const void* data, size_t len)
{
	Md5 m;
	m.Update(data, len);
	return m.HexDigest();
}

bool Md5::File(const std::wstring& path, std::string& hexOut)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	Md5 m;
	std::vector<unsigned char> buf(65536);
	DWORD read = 0;
	while (ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr) && read > 0)
		m.Update(buf.data(), read);
	CloseHandle(h);
	hexOut = m.HexDigest();
	return true;
}

bool Md5::StreamRange(HANDLE h, std::string& hexOut)
{
	Md5 m;
	LARGE_INTEGER zero; zero.QuadPart = 0;
	SetFilePointerEx(h, zero, nullptr, FILE_BEGIN);
	std::vector<unsigned char> buf(65536);
	DWORD read = 0;
	while (ReadFile(h, buf.data(), (DWORD)buf.size(), &read, nullptr) && read > 0)
		m.Update(buf.data(), read);
	hexOut = m.HexDigest();
	return true;
}

}
