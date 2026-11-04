#include "Signature.h"
#include "Common.h"
#include <wincrypt.h>
#include <cstring>
#include <algorithm>

namespace el {

static const char kPem[] =
"-----BEGIN PUBLIC KEY-----\n"
"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDPMMs3JBXN6UFhsUEH0E2W6/+8\n"
"NXmqIl4SXLBxoO7kRSXyybiUxBt46sBDDHIJYFxB6vHaGKmO7B+gkhBOXJjsbQix\n"
"YlL5/rBXFKe18JNuJr2+tUYeY2BUjIP0vA0rD1y9DuvQOYxugbwkecwfXdexeeCe\n"
"rjv4Nq1hoZHQy83iowIDAQAB\n"
"-----END PUBLIC KEY-----\n";

static const char kMarker[] = "-----BEGIN ELEMENT SIGNATURE-----";
static const size_t kMarkerLen = sizeof(kMarker) - 1;

static bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER sz;
	GetFileSizeEx(h, &sz);
	out.resize((size_t)sz.QuadPart);
	DWORD read = 0, total = 0;
	while (total < out.size() &&
		ReadFile(h, out.data() + total, (DWORD)(out.size() - total), &read, nullptr) && read > 0)
		total += read;
	CloseHandle(h);
	out.resize(total);
	return true;
}

bool VerifySignedFile(const std::wstring& path)
{
	std::vector<uint8_t> data;
	if (!ReadAll(path, data)) return false;

	size_t signedLen = data.size();
	size_t sigStart = data.size();
	size_t lineStart = 0;
	while (lineStart < data.size())
	{
		size_t eol = lineStart;
		while (eol < data.size() && data[eol] != '\n') ++eol;
		size_t lineLen = eol - lineStart;
		size_t cmpLen = lineLen < kMarkerLen ? lineLen : kMarkerLen;
		if (cmpLen == kMarkerLen && memcmp(data.data() + lineStart, kMarker, kMarkerLen) == 0)
		{
			signedLen = lineStart;
			sigStart = (eol < data.size()) ? eol + 1 : data.size();
			break;
		}
		lineStart = (eol < data.size()) ? eol + 1 : data.size();
	}
	if (sigStart > data.size()) return false;

	std::string sigText((const char*)data.data() + sigStart, data.size() - sigStart);

	HCRYPTPROV hProv = 0;
	if (!CryptAcquireContextW(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
		return false;

	bool ok = false;
	HCRYPTKEY hKey = 0;
	CERT_PUBLIC_KEY_INFO* pInfo = nullptr;
	DWORD infoLen = 0;
	DWORD derLen = 0;

	if (CryptStringToBinaryA(kPem, 0, CRYPT_STRING_BASE64HEADER, nullptr, &derLen, nullptr, nullptr))
	{
		std::vector<uint8_t> der(derLen);
		if (CryptStringToBinaryA(kPem, 0, CRYPT_STRING_BASE64HEADER, der.data(), &derLen, nullptr, nullptr))
		{
			if (CryptDecodeObjectEx(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
				X509_PUBLIC_KEY_INFO, der.data(), derLen, CRYPT_DECODE_ALLOC_FLAG,
				nullptr, &pInfo, &infoLen))
			{
				if (CryptImportPublicKeyInfo(hProv, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
					pInfo, &hKey))
				{
					HCRYPTHASH hHash = 0;
					if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
					{
						if (CryptHashData(hHash, data.data(), (DWORD)signedLen, 0))
						{
							DWORD sigLen = 0;
							if (CryptStringToBinaryA(sigText.c_str(), (DWORD)sigText.size(),
								CRYPT_STRING_BASE64, nullptr, &sigLen, nullptr, nullptr))
							{
								std::vector<uint8_t> sig(sigLen);
								if (CryptStringToBinaryA(sigText.c_str(), (DWORD)sigText.size(),
									CRYPT_STRING_BASE64, sig.data(), &sigLen, nullptr, nullptr))
								{
									std::reverse(sig.begin(), sig.end());
									if (CryptVerifySignatureA(hHash, sig.data(), sigLen, hKey, nullptr, 0))
										ok = true;
								}
							}
						}
						CryptDestroyHash(hHash);
					}
				}
			}
		}
	}

	if (pInfo) LocalFree(pInfo);
	if (hKey) CryptDestroyKey(hKey);
	CryptReleaseContext(hProv, 0);
	return ok;
}

}
