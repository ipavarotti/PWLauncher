#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace el {

struct AppConfig
{
	std::wstring rootDir;
	std::wstring exeDir;
	std::wstring baseDir;      // diretorio base do launcher (== exeDir no layout de referencia)
	std::wstring configJson;   // <baseDir>\config.json
	bool hasConfigJson = false;

	std::wstring updateCfg;
	std::wstring serverListFile;
	std::wstring pidIni;
	std::wstring launcherSkin;

	std::wstring patcherServerName;
	std::wstring serverBase;
	std::wstring jsonBaseUrl;  // BaseUrl vindo do config.json

	std::wstring installDirName; // "element"
	std::wstring elementDir;
	std::wstring installDir;     // == elementDir (diretorio onde ficam os arquivos do jogo)
	std::wstring elementConfigDir;
	std::wstring launcherConfigDir;
	std::wstring launcherVersionFile;
	std::wstring elementVersionFile;
	std::wstring elementNewVersionFile;
	std::wstring elementFullListFile;
	std::wstring elementUpdateListFile;
	std::wstring elementUpdateStateFile;
	std::wstring elementLogFile;
	std::wstring elementLauncherLogFile;
	std::wstring tempFile;
	std::wstring tempDir;
	std::wstring driversDir;

	std::wstring clientExe;
	int pid;

	// ----- config.json (estilo do launcher de referencia) -----
	bool launcherSelfUpdate = false;
	std::wstring language = L"pt";
	bool useListMode = false;

	std::wstring verLauncher;
	int verGame = -1;            // -1 == null
	std::wstring verPackDll;

	std::wstring exeX86 = L"elementclient.exe";
	std::wstring argsX86;
	std::wstring exeX64 = L"x64\\elementclient_64.exe";
	std::wstring argsX64;
	bool use64BitOn64OS = true;
	bool runAsAdmin = false;
	bool closeLauncherOnStart = true;
	std::wstring workingDir;

	std::wstring clanVersion;
	std::wstring clanVersionUrl;
	std::wstring clanPngUrl;
	std::wstring clanTxtUrl;

	std::wstring linkWebsite;
	std::wstring linkProfile;
	std::wstring linkDiscord;

	int zlibLevel = 9;           // nivel zlib p/ verificar local (bate com o CPWCreator)

	bool verifySignature;
	bool verifyLocal;
	bool autoClose;
	bool silent;
	bool fullCheck;
	bool verifyMode;             // /verify | /repair: varre tudo e repara faltando/corrompido
	bool useLocalUpdate;
	bool usePack;
	std::wstring localUpdatePack;
	std::wstring updateServerArg;

	int connectTimeoutMs;
	int receiveTimeoutMs;
	int reconnectMs;
	int totalTimeoutMs;
	int maxRetries;

	AppConfig();

	void ComputePaths();
	bool Load(int argc, wchar_t** argv);
	bool LoadServerList();
	bool LoadJsonConfig();
	bool SaveJsonConfig() const;
};

}
