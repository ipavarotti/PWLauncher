#include "Config.h"
#include "Common.h"
#include "BuildConfig.h"
#include "Json.h"
#include <vector>
#include <cstdio>
#include <cstring>

namespace el {

AppConfig::AppConfig()
{
	pid = 1;
	verifySignature = kVerifySignature;
	verifyLocal = true;
	autoClose = false;
	silent = false;
	fullCheck = false;
	verifyMode = false;
	useLocalUpdate = false;
	usePack = true;
	connectTimeoutMs = 30000;
	receiveTimeoutMs = 60000;
	reconnectMs = 10000;
	totalTimeoutMs = 60000;
	maxRetries = 5;
	installDirName = L"element";
}

static std::wstring ParentDir(const std::wstring& dir)
{
	size_t pos = dir.find_last_of(L"\\/");
	if (pos == std::wstring::npos) return dir;
	return dir.substr(0, pos);
}

static std::wstring NormalizePath(const std::wstring& path)
{
	wchar_t buf[MAX_PATH * 4] = { 0 };
	DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH * 4, buf, nullptr);
	if (n == 0 || n >= MAX_PATH * 4) return path;
	std::wstring out(buf, n);
	while (out.size() > 3 && (out.back() == L'\\' || out.back() == L'/'))
		out.pop_back();
	return out;
}

void AppConfig::ComputePaths()
{
	exeDir = GetModuleDir();
	baseDir = exeDir;
	configJson = JoinPath(baseDir, L"config.json");
	hasConfigJson = FileExists(configJson);

	// Detecta o layout do cliente:
	//  - exe em <cliente>\launcher\  -> raiz = pasta-pai (classico)
	//  - exe em <cliente>\           -> raiz = o proprio exeDir (estilo referencia)
	std::wstring leaf = exeDir;
	size_t slash = leaf.find_last_of(L"\\/");
	if (slash != std::wstring::npos) leaf = leaf.substr(slash + 1);
	rootDir = (ToLower(leaf) == L"launcher") ? ParentDir(exeDir) : exeDir;

	updateCfg = JoinPath(JoinPath(JoinPath(rootDir, L"config"), L"patcher"), L"update.cfg");
	serverListFile = JoinPath(JoinPath(JoinPath(rootDir, L"patcher"), L"server"), L"updateserver.txt");
	pidIni = JoinPath(JoinPath(JoinPath(rootDir, L"patcher"), L"server"), L"pid.ini");
	launcherSkin = JoinPath(JoinPath(exeDir, L"skin"), L"skin.ini");

	elementDir = JoinPath(rootDir, L"element");
	installDir = elementDir;
	elementConfigDir = JoinPath(JoinPath(rootDir, L"config"), L"element");
	launcherConfigDir = JoinPath(JoinPath(rootDir, L"config"), L"launcher");
	launcherVersionFile = JoinPath(launcherConfigDir, L"version.sw");
	elementVersionFile = JoinPath(elementDir, L"version.sw");
	elementNewVersionFile = JoinPath(elementDir, L"newver.sw");
	elementFullListFile = JoinPath(elementDir, L"FullList.sw");
	elementUpdateListFile = JoinPath(elementDir, L"inc.sw");
	elementUpdateStateFile = JoinPath(elementDir, L"ustate.sw");
	elementLogFile = JoinPath(baseDir, L"update.log");
	elementLauncherLogFile = JoinPath(baseDir, L"launcher.log");
	tempFile = JoinPath(elementDir, L"tempfile.dat");
	tempDir = JoinPath(elementDir, L"temp");
	driversDir = JoinPath(elementDir, L"drivers");
	clientExe = JoinPath(elementDir, L"elementclient.exe");
}

static std::wstring ReadTextAuto(const std::wstring& path)
{
	std::vector<uint8_t> bytes;
	if (!ReadFileBytes(path, bytes) || bytes.empty()) return std::wstring();
	if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
		return std::wstring((const wchar_t*)(bytes.data() + 2), (bytes.size() - 2) / 2);
	if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
	{
		std::wstring w;
		for (size_t i = 2; i + 1 < bytes.size(); i += 2)
			w.push_back((wchar_t)((bytes[i] << 8) | bytes[i + 1]));
		return w;
	}
	std::string a((const char*)bytes.data(), bytes.size());
	return Widen(a, CP_ACP);
}

static std::wstring JsonW(const json::Value& obj, const char* key, const std::wstring& def)
{
	std::string s = obj.GetString(key, "");
	return s.empty() ? def : Widen(s, CP_UTF8);
}

bool AppConfig::LoadJsonConfig()
{
	if (!hasConfigJson) return false;
	std::string text;
	if (!ReadFileUtf8(configJson, text) || text.empty()) return false;

	json::Value root; std::string jerr;
	if (!json::Parse(text, root, jerr) || !root.IsObject()) return false;

	jsonBaseUrl = JsonW(root, "BaseUrl", L"");
	if (const json::Value* us = root.Find("UpdateServer"))
	{
		std::wstring b = JsonW(*us, "BaseUrl", L"");
		if (!b.empty()) jsonBaseUrl = b;
	}

	std::wstring inst = JsonW(root, "InstallDir", L"");
	if (!inst.empty())
	{
		installDirName = inst;
		std::wstring resolved = NormalizePath(JoinPath(baseDir, inst));
		elementDir = resolved;
		installDir = resolved;
		driversDir = JoinPath(elementDir, L"drivers");
		clientExe = JoinPath(elementDir, L"elementclient.exe");
	}

	language = JsonW(root, "Language", language);
	launcherSelfUpdate = root.GetInt("LauncherSelfUpdate", 0) != 0;
	useListMode = root.GetInt("UseListMode", 0) != 0;
	if (root.Find("CompressionLevel")) zlibLevel = (int)root.GetInt("CompressionLevel", 9);

	if (const json::Value* vs = root.Find("Versions"))
	{
		verLauncher = JsonW(*vs, "Launcher", verLauncher);
		const json::Value* g = vs->Find("Game");
		if (g && g->IsNumber()) verGame = (int)g->num;
		verPackDll = JsonW(*vs, "PackDll", verPackDll);
	}

	if (const json::Value* lc = root.Find("Launch"))
	{
		exeX86 = JsonW(*lc, "ExeX86", exeX86);
		argsX86 = JsonW(*lc, "ArgsX86", argsX86);
		exeX64 = JsonW(*lc, "ExeX64", exeX64);
		argsX64 = JsonW(*lc, "ArgsX64", argsX64);
		if (lc->Find("Use64BitOn64OS")) use64BitOn64OS = lc->GetInt("Use64BitOn64OS", 1) != 0;
		if (lc->Find("RunAsAdmin"))      runAsAdmin = lc->GetInt("RunAsAdmin", 0) != 0;
		if (lc->Find("CloseLauncherOnStart")) closeLauncherOnStart = lc->GetInt("CloseLauncherOnStart", 1) != 0;
		workingDir = JsonW(*lc, "WorkingDir", workingDir);
	}

	if (const json::Value* ci = root.Find("ClanIcons"))
	{
		clanVersion = JsonW(*ci, "Version", clanVersion);
		clanVersionUrl = JsonW(*ci, "VersionUrl", clanVersionUrl);
		clanPngUrl = JsonW(*ci, "PngUrl", clanPngUrl);
		clanTxtUrl = JsonW(*ci, "TxtUrl", clanTxtUrl);
	}

	if (const json::Value* lk = root.Find("Links"))
	{
		linkWebsite = JsonW(*lk, "Website", linkWebsite);
		linkProfile = JsonW(*lk, "Profile", linkProfile);
		linkDiscord = JsonW(*lk, "Discord", linkDiscord);
	}

	return true;
}

bool AppConfig::SaveJsonConfig() const
{
	json::Value root = json::MakeObject();
	json::Set(root, "UpdateServer", json::MakeObject());

	json::Value us = json::MakeObject();
	json::Set(us, "BaseUrl", Narrow(serverBase, CP_UTF8));
	json::Set(root, "UpdateServer", us);

	json::Set(root, "InstallDir", Narrow(installDirName, CP_UTF8));
	json::Set(root, "LauncherSelfUpdate", launcherSelfUpdate);
	json::Set(root, "Language", Narrow(language, CP_UTF8));
	json::Set(root, "UseListMode", useListMode);

	json::Value vs = json::MakeObject();
	json::Set(vs, "Launcher", Narrow(verLauncher, CP_UTF8));
	if (verGame >= 0) json::Set(vs, "Game", verGame);
	json::Set(vs, "PackDll", Narrow(verPackDll, CP_UTF8));
	json::Set(root, "Versions", vs);

	json::Value lc = json::MakeObject();
	json::Set(lc, "ExeX86", Narrow(exeX86, CP_UTF8));
	json::Set(lc, "ArgsX86", Narrow(argsX86, CP_UTF8));
	json::Set(lc, "ExeX64", Narrow(exeX64, CP_UTF8));
	json::Set(lc, "ArgsX64", Narrow(argsX64, CP_UTF8));
	json::Set(lc, "Use64BitOn64OS", use64BitOn64OS);
	json::Set(lc, "RunAsAdmin", runAsAdmin);
	json::Set(lc, "CloseLauncherOnStart", closeLauncherOnStart);
	json::Set(lc, "WorkingDir", Narrow(workingDir, CP_UTF8));
	json::Set(root, "Launch", lc);

	json::Value ci = json::MakeObject();
	json::Set(ci, "Version", Narrow(clanVersion, CP_UTF8));
	json::Set(ci, "VersionUrl", Narrow(clanVersionUrl, CP_UTF8));
	json::Set(ci, "PngUrl", Narrow(clanPngUrl, CP_UTF8));
	json::Set(ci, "TxtUrl", Narrow(clanTxtUrl, CP_UTF8));
	json::Set(root, "ClanIcons", ci);

	json::Value lk = json::MakeObject();
	json::Set(lk, "Website", Narrow(linkWebsite, CP_UTF8));
	json::Set(lk, "Profile", Narrow(linkProfile, CP_UTF8));
	json::Set(lk, "Discord", Narrow(linkDiscord, CP_UTF8));
	json::Set(root, "Links", lk);

	return WriteFileUtf8(configJson, json::Write(root, true));
}

bool AppConfig::LoadServerList()
{
	std::wstring embedded(kEmbeddedUpdateServer);
	bool cmdUrl = !updateServerArg.empty() && updateServerArg.find(L"http") == 0;

	if (kEmbeddedServerOverride && !cmdUrl && jsonBaseUrl.empty())
	{
		serverBase = embedded;
		if (!serverBase.empty() && serverBase.back() != L'/') serverBase += L'/';
		return !serverBase.empty();
	}

	std::wstring text = ReadTextAuto(serverListFile);

	std::wstring selected;
	std::wstring firstUrl;
	size_t i = 0;
	while (i < text.size())
	{
		size_t q1 = text.find(L'"', i);
		if (q1 == std::wstring::npos) break;
		size_t q2 = text.find(L'"', q1 + 1);
		if (q2 == std::wstring::npos) break;
		std::wstring name = text.substr(q1 + 1, q2 - q1 - 1);
		size_t q3 = text.find(L'"', q2 + 1);
		if (q3 == std::wstring::npos) break;
		size_t q4 = text.find(L'"', q3 + 1);
		if (q4 == std::wstring::npos) break;
		std::wstring url = text.substr(q3 + 1, q4 - q3 - 1);
		if (firstUrl.empty()) firstUrl = url;
		if (!updateServerArg.empty() && name == updateServerArg) { selected = url; break; }
		i = q4 + 1;
	}

	if (selected.empty())
	{
		if (cmdUrl)
			selected = updateServerArg;
		else if (!jsonBaseUrl.empty())
			selected = jsonBaseUrl;          // config.json tem prioridade (estilo referencia)
		else if (!firstUrl.empty())
			selected = firstUrl;
		else
			selected = embedded;
	}
	serverBase = selected;
	if (!serverBase.empty() && serverBase.back() != L'/') serverBase += L'/';
	return !serverBase.empty();
}

bool AppConfig::Load(int argc, wchar_t** argv)
{
	ComputePaths();
	LoadJsonConfig();        // pode ajustar installDir/layout/servidor

	bool pidFromCmd = false;

	for (int i = 1; i < argc; ++i)
	{
		std::wstring a = argv[i];
		if (a == L"/fullcheck" || a == L"-fullcheck") fullCheck = true;
		else if (a == L"/hide" || a == L"-hide") silent = true;
		else if (a == L"/autoclose" || a == L"-autoclose") autoClose = true;
		else if (a == L"/nopack" || a == L"-nopack") usePack = false;
		else if (a == L"/verify" || a == L"-verify" || a.rfind(L"/verify:", 0) == 0 || a == L"/repair" || a == L"-repair")
		{ verifyMode = true; fullCheck = true; }
		else if (a.rfind(L"/updateserver:", 0) == 0) updateServerArg = a.substr(14);
		else if (a.rfind(L"-updateserver:", 0) == 0) updateServerArg = a.substr(14);
		else if (a.rfind(L"/localupdate:", 0) == 0) { useLocalUpdate = true; localUpdatePack = a.substr(13); }
		else if (a.rfind(L"/pid:", 0) == 0) { pid = _wtoi(a.c_str() + 5); pidFromCmd = true; }
	}

	std::wstring cfgText = ReadTextAuto(updateCfg);
	std::wstring selectedName;
	{
		std::vector<std::wstring> lines = Split(cfgText, L'\n');
		for (std::wstring& raw : lines)
		{
			std::wstring line = Trim(raw);
			if (line.empty()) continue;
			size_t sp = line.find_first_of(L" \t");
			if (sp == std::wstring::npos) continue;
			std::wstring key = line.substr(0, sp);
			std::wstring val = Trim(line.substr(sp + 1));
			if (key == L"UpdateServerSelect") selectedName = val;
		}
	}

	if (!pidFromCmd)
	{
		int filePid = GetPrivateProfileIntW(L"Version", L"pid", 0, pidIni.c_str());
		if (filePid > 0)
			pid = filePid;
		else if (kEmbeddedPid > 0)
			pid = kEmbeddedPid;
	}

	if (updateServerArg.empty() && !selectedName.empty())
		updateServerArg = selectedName;

	// Corrige arquivos que ficaram como pasta por versoes antigas.
	{
		const std::wstring* files[] = {
			&elementNewVersionFile, &elementFullListFile, &elementUpdateListFile,
			&elementUpdateStateFile, &tempFile, &elementVersionFile,
			&launcherVersionFile, &elementLogFile, &elementLauncherLogFile
		};
		for (const std::wstring* f : files)
			EnsureWritablePath(*f);
	}

	LoadServerList();
	return true;
}

}
