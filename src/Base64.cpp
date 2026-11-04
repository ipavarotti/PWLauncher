#include "Base64.h"
#include "Common.h"
#include <cstring>

namespace el {

static const char kAlphabet[65] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";

static unsigned char kDecode[256];
static bool kInit = false;

static void InitTable()
{
	if (kInit) return;
	memset(kDecode, 255, sizeof(kDecode));
	for (int i = 0; i < 64; ++i)
		kDecode[(unsigned char)kAlphabet[i]] = (unsigned char)i;
	kDecode[(unsigned char)'='] = 0;
	kInit = true;
}

namespace base64 {

bool IsCode(char c)
{
	InitTable();
	return kDecode[(unsigned char)c] != 255;
}

std::string Encode(const unsigned char* data, size_t len)
{
	InitTable();
	std::string out;
	out.reserve(((len + 2) / 3) * 4);
	size_t i = 0;
	for (; i + 3 <= len; i += 3)
	{
		uint32_t c = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
		out.push_back(kAlphabet[(c >> 18) & 63]);
		out.push_back(kAlphabet[(c >> 12) & 63]);
		out.push_back(kAlphabet[(c >> 6) & 63]);
		out.push_back(kAlphabet[c & 63]);
	}
	size_t rem = len - i;
	if (rem == 1)
	{
		uint32_t c = (uint32_t)data[i] << 16;
		out.push_back(kAlphabet[(c >> 18) & 63]);
		out.push_back(kAlphabet[(c >> 12) & 63]);
		out.push_back('=');
		out.push_back('=');
	}
	else if (rem == 2)
	{
		uint32_t c = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
		out.push_back(kAlphabet[(c >> 18) & 63]);
		out.push_back(kAlphabet[(c >> 12) & 63]);
		out.push_back(kAlphabet[(c >> 6) & 63]);
		out.push_back('=');
	}
	return out;
}

std::string Encode(const std::string& data)
{
	return Encode((const unsigned char*)data.data(), data.size());
}

std::vector<unsigned char> Decode(const std::string& text)
{
	InitTable();
	std::vector<unsigned char> out;
	size_t len = text.size();
	size_t i = 0;
	while (i + 4 <= len)
	{
		uint32_t c = ((uint32_t)kDecode[(unsigned char)text[i]] << 18)
			| ((uint32_t)kDecode[(unsigned char)text[i + 1]] << 12)
			| ((uint32_t)kDecode[(unsigned char)text[i + 2]] << 6)
			| ((uint32_t)kDecode[(unsigned char)text[i + 3]]);
		out.push_back((unsigned char)((c >> 16) & 0xFF));
		out.push_back((unsigned char)((c >> 8) & 0xFF));
		out.push_back((unsigned char)(c & 0xFF));
		i += 4;
	}
	return out;
}

}

static std::string g_prefix;

static std::wstring DecodeSegment(const std::string& seg)
{
	std::vector<unsigned char> raw = base64::Decode(seg);
	size_t n = raw.size();
	for (size_t k = 0; k < raw.size(); ++k)
		if (raw[k] == 0) { n = k; break; }
	std::string bytes(raw.begin(), raw.begin() + n);
	return Widen(bytes, CP_UTF8);
}

static void TransferPath(std::string& name)
{
	if (!name.empty() && name[0] == '/')
	{
		std::string fn1 = "." + name;
		name = fn1;
		size_t i = name.size() - 1;
		while (i > 0 && name[i] != '/') --i;
		g_prefix = name.substr(0, i + 1);
	}
	else
	{
		name = g_prefix + name;
	}
}

std::wstring DecodeFileName(const std::string& coded, std::string& outPrefix)
{
	std::string src = coded;
	TransferPath(src);
	outPrefix = g_prefix;

	std::wstring result;
	size_t len = src.size();
	size_t j = 0;
	size_t i = 0;
	for (; i < len; ++i)
	{
		if (!base64::IsCode(src[i]))
		{
			if (i != j)
			{
				result += DecodeSegment(src.substr(j, i - j));
			}
			result.push_back((wchar_t)(unsigned char)src[i]);
			j = i + 1;
		}
	}
	if (i != j)
	{
		result += DecodeSegment(src.substr(j, i - j));
	}
	return result;
}

std::wstring DecodeFileName(const std::string& coded)
{
	std::string dummy;
	return DecodeFileName(coded, dummy);
}

}
