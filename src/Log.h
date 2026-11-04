#pragma once
#include <windows.h>
#include <string>

namespace el {

class Log
{
public:
	static void Init(const std::wstring& path);
	static void Write(const std::wstring& line);
	static void Write(const std::string& line);
	static void Error(const std::wstring& line);
	static const std::wstring& Path();
private:
	static CRITICAL_SECTION s_cs;
	static bool s_init;
	static std::wstring s_path;
};

}
