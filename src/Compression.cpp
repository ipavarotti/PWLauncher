#include "Compression.h"
#include "Md5.h"
#include "Common.h"
#include <zlib.h>
#include <cstring>

namespace el {

static int g_level = 9;
void SetCompressionLevel(int level) { g_level = (level < 0 || level > 9) ? 9 : level; }
int  CompressionLevel() { return g_level; }

std::vector<uint8_t> PackBuffer(const uint8_t* data, size_t len)
{
	// Igual ao CPWCreator: prefixa o tamanho original (4B LE) e SEMPRE grava o
	// stream zlib completo no nivel configurado (sem fallback para "cru").
	std::vector<uint8_t> out;
	out.resize(len + 4 + 64);
	uLongf destLen = (uLongf)out.size() - 4;
	int ret = compress2(out.data() + 4, &destLen, data, (uLong)len, g_level);
	if (ret != Z_OK)
	{
		out.resize(4);
		return out;
	}

	uint32_t sizeField = (uint32_t)len;
	out[0] = (uint8_t)(sizeField & 0xFF);
	out[1] = (uint8_t)((sizeField >> 8) & 0xFF);
	out[2] = (uint8_t)((sizeField >> 16) & 0xFF);
	out[3] = (uint8_t)((sizeField >> 24) & 0xFF);
	out.resize(destLen + 4);
	return out;
}

bool UnpackBuffer(const uint8_t* data, size_t len, std::vector<uint8_t>& rawOut)
{
	if (len < 4) return false;
	uint32_t realSize = (uint32_t)data[0] | ((uint32_t)data[1] << 8)
		| ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);

	if ((uint64_t)realSize + 4 == len)
	{
		rawOut.assign(data + 4, data + len);
		return true;
	}

	rawOut.resize(realSize);
	uLongf destLen = (uLongf)realSize;
	int ret = uncompress(rawOut.data(), &destLen, data + 4, (uLong)(len - 4));
	if (ret != Z_OK) return false;
	rawOut.resize(destLen);
	return true;
}

bool PackFile(const std::wstring& src, std::vector<uint8_t>& packedOut)
{
	HANDLE h = CreateFileW(src.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;

	LARGE_INTEGER sz;
	if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); return false; }
	std::vector<uint8_t> raw((size_t)sz.QuadPart);
	DWORD read = 0, total = 0;
	while (total < (DWORD)raw.size() &&
		ReadFile(h, raw.data() + total, (DWORD)raw.size() - total, &read, nullptr) && read > 0)
		total += read;
	CloseHandle(h);

	if (total != raw.size()) return false;
	packedOut = PackBuffer(raw.data(), raw.size());
	return true;
}

bool UnpackFileTo(const std::wstring& packedSrc, const std::wstring& dest)
{
	std::vector<uint8_t> packed;
	if (!PackFile(packedSrc, packed)) return false;
	std::vector<uint8_t> raw;
	if (!UnpackBuffer(packed.data(), packed.size(), raw)) return false;

	SetFileAttributesW(dest.c_str(), FILE_ATTRIBUTE_NORMAL);
	HANDLE h = CreateFileW(dest.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	bool ok = true;
	size_t off = 0;
	while (off < raw.size())
	{
		if (!WriteFile(h, raw.data() + off, (DWORD)(raw.size() - off), &written, nullptr) || written == 0)
		{
			ok = false;
			break;
		}
		off += written;
	}
	CloseHandle(h);
	return ok;
}

bool PackedMd5OfFile(const std::wstring& path, std::string& hexOut)
{
	std::vector<uint8_t> packed;
	if (!PackFile(path, packed)) return false;
	hexOut = Md5::Buffer(packed.data(), packed.size());
	return true;
}

std::string PackedMd5OfBuffer(const uint8_t* data, size_t len)
{
	std::vector<uint8_t> packed = PackBuffer(data, len);
	return Md5::Buffer(packed.data(), packed.size());
}

}
