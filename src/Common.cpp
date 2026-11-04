#include "Common.h"
#include <cstdarg>
#include <cstdio>

namespace el {

std::wstring Widen(const std::string& s, UINT codePage)
{
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(codePage, 0, s.c_str(), (int)s.size(), nullptr, 0);
	if (n <= 0) return std::wstring();
	std::wstring out(n, L'\0');
	MultiByteToWideChar(codePage, 0, s.c_str(), (int)s.size(), &out[0], n);
	return out;
}

std::string Narrow(const std::wstring& s, UINT codePage)
{
	if (s.empty()) return std::string();
	int n = WideCharToMultiByte(codePage, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
	if (n <= 0) return std::string();
	std::string out(n, '\0');
	WideCharToMultiByte(codePage, 0, s.c_str(), (int)s.size(), &out[0], n, nullptr, nullptr);
	return out;
}

std::wstring Trim(const std::wstring& s)
{
	size_t a = s.find_first_not_of(L" \t\r\n");
	if (a == std::wstring::npos) return std::wstring();
	size_t b = s.find_last_not_of(L" \t\r\n");
	return s.substr(a, b - a + 1);
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t delim)
{
	std::vector<std::wstring> out;
	std::wstring cur;
	for (wchar_t c : s)
	{
		if (c == delim) { out.push_back(cur); cur.clear(); }
		else cur.push_back(c);
	}
	out.push_back(cur);
	return out;
}

std::wstring GetModuleDir()
{
	wchar_t buf[MAX_PATH] = { 0 };
	GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring p(buf);
	size_t pos = p.find_last_of(L"\\/");
	if (pos == std::wstring::npos) return L".";
	return p.substr(0, pos);
}

std::wstring JoinPath(const std::wstring& a, const std::wstring& b)
{
	if (a.empty()) return b;
	std::wstring r = a;
	if (r.back() != L'\\' && r.back() != L'/') r += L'\\';
	return r + b;
}

std::wstring ToLower(std::wstring s)
{
	for (wchar_t& c : s) c = (wchar_t)towlower(c);
	return s;
}

bool FileExists(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool DirExists(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

int64_t FileSize(const std::wstring& path)
{
	WIN32_FILE_ATTRIBUTE_DATA fad;
	if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
		return -1;
	LARGE_INTEGER li;
	li.HighPart = fad.nFileSizeHigh;
	li.LowPart = fad.nFileSizeLow;
	return li.QuadPart;
}

static bool MakeDirSingle(const std::wstring& path)
{
	if (path.empty()) return false;
	return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool MakeDirs(const std::wstring& path)
{
	if (path.empty()) return false;
	std::wstring norm = path;
	for (wchar_t& c : norm) if (c == L'/') c = L'\\';

	size_t pos = norm.find_last_of(L'\\');
	if (pos == std::wstring::npos)
		return MakeDirSingle(norm);

	std::wstring prefix = norm.substr(0, pos);
	if (!prefix.empty() && prefix.back() == L':')
		return true;

	if (!DirExists(prefix))
		if (!MakeDirs(prefix)) return false;

	return MakeDirSingle(norm);
}

bool EnsureParentDir(const std::wstring& path)
{
	size_t pos = path.find_last_of(L"\\/");
	if (pos == std::wstring::npos) return true;
	return MakeDirs(path.substr(0, pos));
}

bool RemoveDirIfEmpty(const std::wstring& path)
{
	return RemoveDirectoryW(path.c_str()) != FALSE;
}

std::wstring FormatBytes(uint64_t bytes)
{
	double v = (double)bytes;
	const wchar_t* unit = L"B";
	if (v >= 1024.0 * 1024 * 1024) { v /= 1024.0 * 1024 * 1024; unit = L"GB"; }
	else if (v >= 1024.0 * 1024) { v /= 1024.0 * 1024; unit = L"MB"; }
	else if (v >= 1024.0) { v /= 1024.0; unit = L"KB"; }
	wchar_t buf[64];
	if (unit[0] == L'B')
		swprintf(buf, 64, L"%llu B", (unsigned long long)bytes);
	else
		swprintf(buf, 64, L"%.1f %s", v, unit);
	return buf;
}

std::wstring Format(const wchar_t* fmt, ...)
{
	wchar_t buf[1024];
	va_list args;
	va_start(args, fmt);
	_vsnwprintf(buf, 1024, fmt, args);
	va_end(args);
	buf[1023] = L'\0';
	return buf;
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>& out)
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

bool ReadFileUtf8(const std::wstring& path, std::string& out)
{
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(path, bytes)) return false;
	if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
		out.assign((const char*)bytes.data() + 3, bytes.size() - 3);
	else
		out.assign((const char*)bytes.data(), bytes.size());
	return true;
}

bool WriteFileUtf8(const std::wstring& path, const std::string& text)
{
	if (!EnsureParentDir(path)) return false;
	EnsureWritablePath(path);
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	bool ok = true;
	size_t off = 0;
	while (off < text.size())
	{
		if (!WriteFile(h, text.data() + off, (DWORD)(text.size() - off), &written, nullptr) || written == 0)
		{
			ok = false;
			break;
		}
		off += written;
	}
	CloseHandle(h);
	return ok;
}

static bool DeleteDirRecursive(const std::wstring& dir)
{
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
	if (h != INVALID_HANDLE_VALUE)
	{
		do
		{
			if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
			std::wstring p = dir + L"\\" + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				DeleteDirRecursive(p);
			else
			{
				SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
				DeleteFileW(p.c_str());
			}
		} while (FindNextFileW(h, &fd));
		FindClose(h);
	}
	return RemoveDirectoryW(dir.c_str()) != FALSE;
}

bool ForceRemovePath(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	if (a == INVALID_FILE_ATTRIBUTES) return true;
	if (a & FILE_ATTRIBUTE_DIRECTORY) return DeleteDirRecursive(path);
	SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	return DeleteFileW(path.c_str()) != FALSE;
}

bool EnsureWritablePath(const std::wstring& path)
{
	DWORD a = GetFileAttributesW(path.c_str());
	if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY))
		return ForceRemovePath(path);
	return true;
}

int GetFreeDiskSpaceMB(const std::wstring& path)
{
	ULARGE_INTEGER freeBytes, total, totalFree;
	std::wstring dir = path;
	if (dir.empty()) dir = L".";
	if (!GetDiskFreeSpaceExW(dir.c_str(), &freeBytes, &total, &totalFree))
		return -1;
	return (int)(freeBytes.QuadPart / (1024ull * 1024ull));
}

}
