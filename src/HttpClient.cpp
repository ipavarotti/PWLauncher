#include "HttpClient.h"
#include "Common.h"
#include <wininet.h>
#include <zlib.h>
#include <cstdio>

namespace el {

HttpClient::HttpClient()
	: m_session(nullptr), m_userAgent(L"Mozilla/4.0"), m_connectMs(30000), m_receiveMs(60000)
{
	m_session = InternetOpenW(m_userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG,
		nullptr, nullptr, 0);
	if (m_session)
	{
		InternetSetOptionW(m_session, INTERNET_OPTION_CONNECT_TIMEOUT, &m_connectMs, sizeof(m_connectMs));
		InternetSetOptionW(m_session, INTERNET_OPTION_RECEIVE_TIMEOUT, &m_receiveMs, sizeof(m_receiveMs));
	}
}

HttpClient::~HttpClient()
{
	if (m_session) InternetCloseHandle(m_session);
}

void HttpClient::SetTimeouts(int connectMs, int receiveMs)
{
	m_connectMs = connectMs;
	m_receiveMs = receiveMs;
	if (m_session)
	{
		InternetSetOptionW(m_session, INTERNET_OPTION_CONNECT_TIMEOUT, &m_connectMs, sizeof(m_connectMs));
		InternetSetOptionW(m_session, INTERNET_OPTION_RECEIVE_TIMEOUT, &m_receiveMs, sizeof(m_receiveMs));
	}
}

void HttpClient::SetUserAgent(const std::wstring& ua)
{
	m_userAgent = ua;
	if (m_session) InternetCloseHandle(m_session);
	m_session = InternetOpenW(m_userAgent.c_str(), INTERNET_OPEN_TYPE_PRECONFIG,
		nullptr, nullptr, 0);
	SetTimeouts(m_connectMs, m_receiveMs);
}

bool HttpClient::Download(const std::wstring& url, const std::wstring& destFile,
	const ProgressFn& onProgress, uint64_t resumeFrom, DWORD* httpStatus)
{
	if (httpStatus) *httpStatus = 0;
	if (!m_session) return false;

	URL_COMPONENTS uc = { 0 };
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256] = { 0 };
	wchar_t path[2048] = { 0 };
	uc.lpszHostName = host; uc.dwHostNameLength = 255;
	uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;

	if (!InternetCrackUrlW(url.c_str(), (DWORD)url.size(), 0, &uc))
		return false;

	INTERNET_PORT port = uc.nPort;
	if (port == 0) port = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
	DWORD connFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;

	HINTERNET conn = InternetConnectW(m_session, host, port, nullptr, nullptr,
		INTERNET_SERVICE_HTTP, connFlags, 0);
	if (!conn) return false;

	DWORD openFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | connFlags;
	const wchar_t* accept[] = { L"*/*", nullptr };
	HINTERNET req = HttpOpenRequestW(conn, L"GET", path, nullptr, nullptr, accept, openFlags, 0);
	if (!req) { InternetCloseHandle(conn); return false; }

	HttpAddRequestHeadersW(req, L"Accept: */*\r\n", (DWORD)-1, HTTP_ADDREQ_FLAG_ADD);
	if (resumeFrom > 0)
	{
		std::wstring range = Format(L"Range: bytes=%llu-\r\n", (unsigned long long)resumeFrom);
		HttpAddRequestHeadersW(req, range.c_str(), (DWORD)-1, HTTP_ADDREQ_FLAG_ADD);
	}

	if (!HttpSendRequestW(req, nullptr, 0, nullptr, 0))
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	DWORD status = 0, statusLen = sizeof(status);
	HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusLen, nullptr);
	if (httpStatus) *httpStatus = status;

	if (status != 200 && status != 206)
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	DWORD contentLen = 0, contentLenLen = sizeof(contentLen);
	HttpQueryInfoW(req, HTTP_QUERY_CONTENT_LENGTH | HTTP_QUERY_FLAG_NUMBER, &contentLen, &contentLenLen, nullptr);

	uint64_t base = (status == 206) ? resumeFrom : 0;
	uint64_t total = base + contentLen;

	EnsureWritablePath(destFile);
	HANDLE hf = CreateFileW(destFile.c_str(), GENERIC_WRITE, 0, nullptr,
		(status == 206) ? OPEN_ALWAYS : CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hf == INVALID_HANDLE_VALUE)
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}
	if (base > 0)
	{
		LARGE_INTEGER li; li.QuadPart = (LONGLONG)base;
		SetFilePointerEx(hf, li, nullptr, FILE_BEGIN);
	}
	else SetEndOfFile(hf);

	uint64_t done = base;
	bool aborted = false;
	std::vector<char> buf(65536);
	DWORD read = 0;
	while (InternetReadFile(req, buf.data(), (DWORD)buf.size(), &read) && read > 0)
	{
		DWORD written = 0;
		if (!WriteFile(hf, buf.data(), read, &written, nullptr) || written != read)
			break;
		done += read;
		if (onProgress && !onProgress(done, total))
		{
			aborted = true;
			break;
		}
	}

	CloseHandle(hf);
	InternetCloseHandle(req);
	InternetCloseHandle(conn);
	return !aborted;
}

bool HttpClient::DownloadNoResume(const std::wstring& url, const std::wstring& destFile,
	const ProgressFn& onProgress)
{
	return Download(url, destFile, onProgress, 0, nullptr);
}

bool HttpClient::DownloadInflate(const std::wstring& url, const std::wstring& destFile,
	uint64_t* outRawSize, const ProgressFn& onProgress, DWORD* httpStatus)
{
	if (httpStatus) *httpStatus = 0;
	if (!m_session) return false;

	URL_COMPONENTS uc = { 0 };
	uc.dwStructSize = sizeof(uc);
	wchar_t host[256] = { 0 };
	wchar_t path[2048] = { 0 };
	uc.lpszHostName = host; uc.dwHostNameLength = 255;
	uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
	if (!InternetCrackUrlW(url.c_str(), (DWORD)url.size(), 0, &uc)) return false;

	INTERNET_PORT port = uc.nPort;
	if (port == 0) port = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
	DWORD connFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? INTERNET_FLAG_SECURE : 0;

	HINTERNET conn = InternetConnectW(m_session, host, port, nullptr, nullptr, INTERNET_SERVICE_HTTP, connFlags, 0);
	if (!conn) return false;

	DWORD openFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | connFlags;
	const wchar_t* accept[] = { L"*/*", nullptr };
	HINTERNET req = HttpOpenRequestW(conn, L"GET", path, nullptr, nullptr, accept, openFlags, 0);
	if (!req) { InternetCloseHandle(conn); return false; }

	HttpAddRequestHeadersW(req, L"Accept: */*\r\n", (DWORD)-1, HTTP_ADDREQ_FLAG_ADD);
	if (!HttpSendRequestW(req, nullptr, 0, nullptr, 0))
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	DWORD status = 0, statusLen = sizeof(status);
	HttpQueryInfoW(req, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &status, &statusLen, nullptr);
	if (httpStatus) *httpStatus = status;
	if (status != 200)
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	EnsureWritablePath(destFile);
	HANDLE hf = CreateFileW(destFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (hf == INVALID_HANDLE_VALUE)
	{
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	std::vector<char> in(65536);	std::vector<char> out(65536);

	DWORD read = 0;
	uint64_t rawTotal = 0;
	bool aborted = false;
	bool ok = true;

	uint64_t expectedRaw = 0;
	{
		char header[8];
		int got = 0;
		while (got < 8 && InternetReadFile(req, header + got, 8 - got, &read) && read > 0)
			got += read;
		if (got < 8)
		{
			CloseHandle(hf);
			InternetCloseHandle(req);
			InternetCloseHandle(conn);
			return false;
		}
		expectedRaw = (uint64_t)(unsigned char)header[0]
			| ((uint64_t)(unsigned char)header[1] << 8)
			| ((uint64_t)(unsigned char)header[2] << 16)
			| ((uint64_t)(unsigned char)header[3] << 24)
			| ((uint64_t)(unsigned char)header[4] << 32)
			| ((uint64_t)(unsigned char)header[5] << 40)
			| ((uint64_t)(unsigned char)header[6] << 48)
			| ((uint64_t)(unsigned char)header[7] << 56);
	}

	z_stream zs;
	memset(&zs, 0, sizeof(zs));
	if (inflateInit(&zs) != Z_OK)
	{
		CloseHandle(hf);
		InternetCloseHandle(req);
		InternetCloseHandle(conn);
		return false;
	}

	bool eof = false;
	while (!eof && InternetReadFile(req, in.data(), (DWORD)in.size(), &read) && read > 0)
	{
		zs.next_in = (Bytef*)in.data();
		zs.avail_in = read;
		while (zs.avail_in > 0)
		{
			zs.next_out = (Bytef*)out.data();
			zs.avail_out = (uInt)out.size();
			int r = inflate(&zs, Z_NO_FLUSH);
			if (r != Z_OK && r != Z_STREAM_END)
			{
				ok = false;
				eof = true;
				break;
			}
			size_t produced = out.size() - zs.avail_out;
			if (produced > 0)
			{
				DWORD written = 0;
				if (!WriteFile(hf, out.data(), (DWORD)produced, &written, nullptr) || written != produced)
				{
					ok = false;
					eof = true;
					break;
				}
				rawTotal += produced;
				if (onProgress && !onProgress(rawTotal, expectedRaw))
				{
					aborted = true;
					eof = true;
					break;
				}
			}
			if (r == Z_STREAM_END) { eof = true; break; }
		}
	}

	inflateEnd(&zs);
	CloseHandle(hf);
	InternetCloseHandle(req);
	InternetCloseHandle(conn);

	if (outRawSize) *outRawSize = rawTotal;
	return ok && !aborted && rawTotal == expectedRaw;
}

}
