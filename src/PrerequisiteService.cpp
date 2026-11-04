#include "PrerequisiteService.h"
#include "Common.h"
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

namespace el {

static bool RegValueStr(HKEY root, REGSAM view, const wchar_t* subkey, const wchar_t* value, std::wstring& out)
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
		return false;
	wchar_t buf[512] = { 0 };
	DWORD size = sizeof(buf);
	DWORD type = 0;
	LONG r = RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &size);
	RegCloseKey(key);
	if (r != ERROR_SUCCESS) return false;
	buf[511] = L'\0';
	out.assign(buf);
	return true;
}

static bool RegValueInt(HKEY root, REGSAM view, const wchar_t* subkey, const wchar_t* value, DWORD& out)
{
	HKEY key = nullptr;
	if (RegOpenKeyExW(root, subkey, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
		return false;
	DWORD data = 0, size = sizeof(data), type = 0;
	LONG r = RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)&data, &size);
	RegCloseKey(key);
	if (r != ERROR_SUCCESS) return false;
	out = data;
	return true;
}

PrerequisiteService::PrerequisiteService(const std::wstring& installDir, std::function<void(const std::wstring&)> log)
	: m_log(std::move(log))
{
	m_baseDir = GetModuleDir();
	m_driversDir = JoinPath(installDir, L"drivers");
}

bool PrerequisiteService::EnsureInstalled()
{
	bool ok = true;
	if (!DirExists(m_driversDir))
	{
		if (m_log) m_log(L"STATUS:ui.drivers_missing|" + m_driversDir);
	}

	if (!IsVcRedistInstalled())
	{
		if (m_log) m_log(L"Microsoft Visual C++ 2015-2022 Redistributable nao detectado. Instalando...");
		if (DirExists(m_driversDir)) ok = TryInstallVcRedist() && ok;
	}
	else if (m_log) m_log(L"Microsoft Visual C++ 2015-2022 Redistributable ja instalado.");

	if (!IsDirectX9Installed())
	{
		if (m_log) m_log(L"DirectX 9 nao detectado. Instalando...");
		if (DirExists(m_driversDir)) ok = TryInstallDirectX9() && ok;
	}
	else if (m_log) m_log(L"DirectX 9 ja instalado.");

	return ok;
}

bool PrerequisiteService::IsVcRedistInstalled()
{
	const wchar_t* runtimeKeys[] = {
		L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x64",
		L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\x86"
	};
	for (const wchar_t* k : runtimeKeys)
	{
		DWORD v = 0;
		if (RegValueInt(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, k, L"Installed", v) && v == 1) return true;
		if (RegValueInt(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, k, L"Installed", v) && v == 1) return true;
	}

	const wchar_t* uninstall = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";
	const wchar_t* needles[] = { L"Visual C++ 2015-2022", L"Visual C++ 2015-2019" };
	for (REGSAM view : { KEY_WOW64_64KEY, KEY_WOW64_32KEY })
	{
		HKEY key = nullptr;
		if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, uninstall, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
			continue;
		for (DWORD i = 0;; ++i)
		{
			wchar_t name[256] = { 0 };
			DWORD len = 255;
			if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
			std::wstring display;
			if (RegValueStr(HKEY_LOCAL_MACHINE, view, (std::wstring(uninstall) + L"\\" + name).c_str(), L"DisplayName", display))
			{
				for (const wchar_t* n : needles)
				{
					std::wstring dl = ToLower(display), nl = ToLower(n);
					if (dl.find(nl) != std::wstring::npos) { RegCloseKey(key); return true; }
				}
			}
		}
		RegCloseKey(key);
	}
	return false;
}

bool PrerequisiteService::IsDirectX9Installed()
{
	HMODULE h = LoadLibraryW(L"d3d9.dll");
	if (!h) return false;
	bool hasCreate9 = GetProcAddress(h, "Direct3DCreate9") != nullptr;
	FreeLibrary(h);
	if (!hasCreate9) return false;

	std::wstring ver;
	if (!RegValueStr(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY, L"SOFTWARE\\Microsoft\\DirectX", L"Version", ver))
		RegValueStr(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY, L"SOFTWARE\\Microsoft\\DirectX", L"Version", ver);
	if (ver.empty()) return false;

	unsigned major = 0, minor = 0, b1 = 0, b2 = 0;
	swscanf(ver.c_str(), L"%u.%u.%u.%u", &major, &minor, &b1, &b2);
	if (major != 4) return major > 4;
	if (minor != 9) return minor > 9;
	return (b1 > 0) || (b1 == 0 && b2 >= 904);
}

bool PrerequisiteService::TryInstallVcRedist()
{
	const wchar_t* names[] = { L"vc_redist.x64.exe", L"VC_redist.x64.exe", L"vc_redist.x86.exe", L"VC_redist.x86.exe" };
	for (const wchar_t* n : names)
	{
		std::wstring path = JoinPath(m_driversDir, n);
		if (FileExists(path))
			return RunInstaller(path, L"/install /quiet /norestart");
	}
	if (m_log) m_log(L"Instalador do VC++ nao encontrado em drivers.");
	return false;
}

bool PrerequisiteService::TryInstallDirectX9()
{
	std::wstring redist = JoinPath(m_driversDir, L"directx_Jun2010_redist.exe");
	std::wstring setup = JoinPath(JoinPath(m_driversDir, L"DirectX"), L"DXSETUP.exe");
	std::wstring web = JoinPath(m_driversDir, L"dxwebsetup.exe");

	if (FileExists(redist))
	{
		std::wstring extract = JoinPath(m_baseDir, L"_dxredist");
		MakeDirs(extract);
		if (!RunInstaller(redist, L"/Q /T:\"" + extract + L"\""))
			return false;
		std::wstring dxsetup = JoinPath(extract, L"DXSETUP.exe");
		bool ok = FileExists(dxsetup) && RunInstaller(dxsetup, L"/silent");
		// limpa extracao
		std::wstring cmd = L"cmd /c rmdir /s /q \"" + extract + L"\"";
		STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = { 0 };
		if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			WaitForSingleObject(pi.hProcess, 30000);
			CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
		}
		return ok;
	}
	if (FileExists(setup)) return RunInstaller(setup, L"/silent");
	if (FileExists(web)) return RunInstaller(web, L"/Q");
	if (m_log) m_log(L"Instalador do DirectX nao encontrado em drivers.");
	return false;
}

bool PrerequisiteService::RunInstaller(const std::wstring& exe, const std::wstring& args)
{
	SHELLEXECUTEINFOW sei = { 0 };
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
	sei.lpVerb = L"runas";
	sei.lpFile = exe.c_str();
	sei.lpParameters = args.c_str();
	std::wstring dir = exe;
	size_t pos = dir.find_last_of(L"\\/");
	if (pos != std::wstring::npos) dir = dir.substr(0, pos);
	sei.lpDirectory = dir.c_str();
	sei.nShow = SW_HIDE;

	if (!ShellExecuteExW(&sei))
	{
		if (m_log) m_log(L"Falha ao executar instalador (UAC negado?): " + exe);
		return false;
	}
	if (sei.hProcess)
	{
		DWORD w = WaitForSingleObject(sei.hProcess, 900000);
		DWORD code = (DWORD)-1;
		if (w == WAIT_OBJECT_0) GetExitCodeProcess(sei.hProcess, &code);
		else TerminateProcess(sei.hProcess, 1);
		CloseHandle(sei.hProcess);
		if (m_log) m_log(Format(L"Instalador finalizado (code %u): %s", code, exe.c_str()));
		return code == 0;
	}
	return true;
}

}
