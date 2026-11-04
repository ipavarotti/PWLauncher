#include "Log.h"
#include "Common.h"
#include <cstdio>
#include <ctime>

namespace el {

CRITICAL_SECTION Log::s_cs;
bool Log::s_init = false;
std::wstring Log::s_path;

void Log::Init(const std::wstring& path)
{
	if (!s_init)
	{
		InitializeCriticalSection(&s_cs);
		s_init = true;
	}
	std::wstring dir = path;
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos)
		MakeDirs(dir.substr(0, pos));
	EnsureWritablePath(path);
	s_path = path;
}

const std::wstring& Log::Path() { return s_path; }

void Log::Write(const std::string& line) { Write(Widen(line, CP_ACP)); }

void Log::Write(const std::wstring& line)
{
	if (!s_init) return;
	EnterCriticalSection(&s_cs);

	FILE* f = _wfopen(s_path.c_str(), L"ab");
	if (f)
	{
		SYSTEMTIME st;
		GetLocalTime(&st);
		fwprintf(f, L"%04d-%02d-%02d %02d:%02d:%02d  %ls\r\n",
			st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, line.c_str());
		fclose(f);
	}

	LeaveCriticalSection(&s_cs);
}

void Log::Error(const std::wstring& line) { Write(L"[ERROR] " + line); }

}
