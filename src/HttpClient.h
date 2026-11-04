#pragma once
#include <windows.h>
#include <wininet.h>
#include <string>
#include <functional>
#include <cstdint>

namespace el {

class HttpClient
{
public:
	using ProgressFn = std::function<bool(uint64_t downloaded, uint64_t total)>;

	HttpClient();
	~HttpClient();

	void SetTimeouts(int connectMs, int receiveMs);
	void SetUserAgent(const std::wstring& ua);

	bool Download(const std::wstring& url, const std::wstring& destFile,
		const ProgressFn& onProgress, uint64_t resumeFrom, DWORD* httpStatus = nullptr);

	bool DownloadNoResume(const std::wstring& url, const std::wstring& destFile,
		const ProgressFn& onProgress = nullptr);

	bool DownloadInflate(const std::wstring& url, const std::wstring& destFile,
		uint64_t* outRawSize, const ProgressFn& onProgress, DWORD* httpStatus = nullptr);

private:
	HINTERNET m_session;
	std::wstring m_userAgent;
	int m_connectMs;
	int m_receiveMs;
};

}
