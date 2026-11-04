#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace el {

std::wstring Widen(const std::string& s, UINT codePage = CP_UTF8);
std::string  Narrow(const std::wstring& s, UINT codePage = CP_UTF8);

std::wstring Trim(const std::wstring& s);
std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim);

std::wstring GetModuleDir();
std::wstring JoinPath(const std::wstring& a, const std::wstring& b);
std::wstring ToLower(std::wstring s);
bool  FileExists(const std::wstring& path);
bool  DirExists(const std::wstring& path);
int64_t FileSize(const std::wstring& path);
bool  MakeDirs(const std::wstring& path);
bool  EnsureParentDir(const std::wstring& path);
bool  RemoveDirIfEmpty(const std::wstring& path);

std::wstring FormatBytes(uint64_t bytes);
std::wstring Format(const wchar_t* fmt, ...);

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out);
bool ReadFileUtf8(const std::wstring& path, std::string& out);
bool WriteFileUtf8(const std::wstring& path, const std::string& text);

bool ForceRemovePath(const std::wstring& path);
bool EnsureWritablePath(const std::wstring& path);

int  GetFreeDiskSpaceMB(const std::wstring& path);

}
