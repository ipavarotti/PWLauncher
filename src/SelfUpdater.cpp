#include "SelfUpdater.h"
#include "Common.h"
#include "HttpClient.h"
#include "Md5.h"
#include "Json.h"
#include "Log.h"
#include <shellapi.h>
#include <vector>

namespace el {

static std::wstring GetArgValue(int argc, wchar_t** argv, const wchar_t* key)
{
	std::wstring prefix = std::wstring(key) + L"=";
	for (int i = 1; i < argc; ++i)
	{
		std::wstring a = argv[i];
		if (a.size() >= prefix.size() && _wcsnicmp(a.c_str(), prefix.c_str(), prefix.size()) == 0)
		{
			std::wstring v = a.substr(prefix.size());
			if (!v.empty() && v.front() == L'"' && v.back() == L'"' && v.size() >= 2)
				v = v.substr(1, v.size() - 2);
			return v;
		}
	}
	return std::wstring();
}

static bool HasArg(int argc, wchar_t** argv, const wchar_t* key)
{
	for (int i = 1; i < argc; ++i)
		if (_wcsicmp(argv[i], key) == 0) return true;
	return false;
}

static std::wstring ExeName()
{
	wchar_t buf[MAX_PATH] = { 0 };
	GetModuleFileNameW(nullptr, buf, MAX_PATH);
	std::wstring p(buf);
	size_t pos = p.find_last_of(L"\\/");
	return pos == std::wstring::npos ? p : p.substr(pos + 1);
}

static void UpdateConfigVersions(const std::wstring& exeDir,
	const std::wstring& launcherVer, const std::wstring& dllSha)
{
	std::wstring cfgPath = JoinPath(exeDir, L"config.json");
	std::string text;
	if (!ReadFileUtf8(cfgPath, text)) return;

	json::Value root; std::string jerr;
	if (!json::Parse(text, root, jerr) || !root.IsObject())
		root = json::MakeObject();

	const json::Value* vs = root.Find("Versions");
	json::Value versions = (vs && vs->IsObject()) ? *vs : json::MakeObject();
	if (!launcherVer.empty()) json::Set(versions, "Launcher", Narrow(launcherVer, CP_UTF8));
	if (!dllSha.empty()) json::Set(versions, "PackDll", Narrow(dllSha, CP_UTF8));
	json::Set(root, "Versions", versions);

	WriteFileUtf8(cfgPath, json::Write(root, true));
}

bool SelfUpdater::HandleReplaceMode(int argc, wchar_t** argv)
{
	if (!HasArg(argc, argv, L"--replace") &&
		!HasArg(argc, argv, L"--replace-exe") &&
		!HasArg(argc, argv, L"--replace-dll") &&
		!HasArg(argc, argv, L"--new"))
		return false;

	std::wstring exeName = GetArgValue(argc, argv, L"--exe");
	if (exeName.empty()) exeName = ExeName();
	std::wstring newName = GetArgValue(argc, argv, L"--new");
	if (newName.empty()) newName = GetArgValue(argc, argv, L"--replace-exe");
	std::wstring dllNew = GetArgValue(argc, argv, L"--dll-new");
	if (dllNew.empty()) dllNew = GetArgValue(argc, argv, L"--replace-dll");
	std::wstring targetVer = GetArgValue(argc, argv, L"--target-ver");
	std::wstring dllSha = GetArgValue(argc, argv, L"--dll-target-sha");

	std::wstring exeDir = GetModuleDir();
	std::wstring exePath = JoinPath(exeDir, exeName);
	std::wstring newPath = newName.empty() ? std::wstring() : JoinPath(exeDir, newName);

	// Espera o processo antigo liberar o exe e substitui.
	if (!newPath.empty())
	{
		for (int i = 0; i < 200; ++i)
		{
			if (!FileExists(newPath)) break;
			if (FileExists(exePath))
			{
				if (MoveFileExW(newPath.c_str(), exePath.c_str(), MOVEFILE_REPLACE_EXISTING))
					break;
			}
			else
			{
				if (MoveFileExW(newPath.c_str(), exePath.c_str(), MOVEFILE_REPLACE_EXISTING))
					break;
			}
			Sleep(200);
		}
	}

	// Substitui o pckdll.dll, se fornecido.
	if (!dllNew.empty())
	{
		std::wstring src = JoinPath(exeDir, dllNew);
		std::wstring dst = JoinPath(exeDir, L"pckdll.dll");
		if (FileExists(src))
		{
			for (int i = 0; i < 50; ++i)
			{
				if (MoveFileExW(src.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) break;
				Sleep(200);
			}
		}
	}

	if (!targetVer.empty() || !dllSha.empty())
		UpdateConfigVersions(exeDir, targetVer, dllSha);

	// Reinicia o launcher.
	ShellExecuteW(nullptr, L"open", exePath.c_str(), nullptr, exeDir.c_str(), SW_SHOWNORMAL);
	return true;
}

SelfUpdater::SelfUpdater(AppConfig& cfg, LogFn log)
	: m_cfg(cfg), m_log(std::move(log))
{
}

bool SelfUpdater::NeedsSelfUpdate(std::wstring& remoteVersion)
{
	remoteVersion.clear();
	if (!m_cfg.launcherSelfUpdate) return false;

	HttpClient http;
	http.SetTimeouts(5000, 15000);
	http.SetUserAgent(L"PWLauncher/SelfUpdate");

	std::wstring tempDir = JoinPath(GetModuleDir(), L"_temp");
	MakeDirs(tempDir);
	std::wstring verFile = JoinPath(tempDir, L"launcher.version");

	DWORD status = 0;
	std::wstring url = m_cfg.serverBase + L"launcher/version";
	Log::Write(L"GET " + url);
	if (!http.Download(url, verFile, nullptr, 0, &status))
	{
		Log::Write(Format(L"launcher/version indisponivel (HTTP %u)", status));
		return false;
	}

	std::string text;
	ReadFileUtf8(verFile, text);
	DeleteFileW(verFile.c_str());
	while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ')) text.pop_back();
	std::wstring remote = Trim(Widen(text, CP_UTF8));
	if (remote.empty()) return false;

	std::wstring local = m_cfg.verLauncher;
	if (!local.empty() && ToLower(local) == ToLower(remote))
	{
		Log::Write(L"launcher ja atualizado (v" + local + L")");
		return false;
	}
	remoteVersion = remote;
	Log::Write(L"launcher update disponivel: " + local + L" -> " + remote);
	return true;
}

bool SelfUpdater::PerformSelfUpdate(const std::wstring& remoteVersion)
{
	std::wstring exeDir = GetModuleDir();
	std::wstring exeName = ExeName();
	std::wstring url = m_cfg.serverBase + L"launcher/" + exeName;

	std::wstring tempDir = JoinPath(exeDir, L"_temp");
	MakeDirs(tempDir);
	std::wstring part = JoinPath(tempDir, exeName + L".part");

	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);
	http.SetUserAgent(L"PWLauncher/SelfUpdate");
	DWORD status = 0;
	if (!http.Download(url, part, nullptr, 0, &status))
	{
		if (m_log) m_log(Format(L"Falha ao baixar launcher (HTTP %u)", status));
		return false;
	}

	// Verifica .md5 opcional ao lado do binario no servidor.
	std::wstring md5File = JoinPath(tempDir, exeName + L".md5");
	DWORD md5Status = 0;
	if (http.Download(url + L".md5", md5File, nullptr, 0, &md5Status))
	{
		std::string mdtext; ReadFileUtf8(md5File, mdtext);
		DeleteFileW(md5File.c_str());
		std::wstring expect = Trim(Widen(mdtext, CP_UTF8));
		std::string actual;
		Md5::File(part, actual);
		if (!expect.empty() && ToLower(expect) != ToLower(Widen(actual, CP_ACP)))
		{
			if (m_log) m_log(L"MD5 do launcher divergente");
			DeleteFileW(part.c_str());
			return false;
		}
	}

	std::wstring newPath = JoinPath(exeDir, exeName + L".new");
	DeleteFileW(newPath.c_str());
	if (!MoveFileExW(part.c_str(), newPath.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		if (m_log) m_log(L"Falha ao preparar o launcher novo");
		return false;
	}

	std::wstring args = L"--replace --exe=" + exeName + L" --new=" + exeName + L".new";
	if (!remoteVersion.empty()) args += L" --target-ver=" + remoteVersion;

	std::wstring cmd = L"\"" + JoinPath(exeDir, exeName) + L"\" " + args;
	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = { 0 };
	if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, exeDir.c_str(), &si, &pi))
	{
		if (m_log) m_log(L"Falha ao iniciar o modo replace");
		return false;
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if (m_log) m_log(L"Self-update agendado: " + remoteVersion);
	return true;
}

}
