#include "Common.h"
#include "Config.h"
#include "UpdateEngine.h"
#include "LauncherWindow.h"
#include "Localization.h"
#include "SelfUpdater.h"
#include "PrerequisiteService.h"
#include "GuildIconsUpdater.h"
#include "NewsService.h"
#include "Log.h"
#include <windows.h>
#include <gdiplus.h>
#include <thread>
#include <shellapi.h>
#include <cstdio>

using namespace el;

static void WriteCrashLog(const std::wstring& text)
{
	std::wstring path = JoinPath(GetModuleDir(), L"launcher_crash.log");
	WriteFileUtf8(path, Narrow(text, CP_UTF8));
}

static LONG WINAPI CrashHandler(EXCEPTION_POINTERS* info)
{
	wchar_t buf[256];
	swprintf(buf, 256, L"[crash] exception 0x%08X\r\n", info && info->ExceptionRecord ? (unsigned)info->ExceptionRecord->ExceptionCode : 0u);
	WriteCrashLog(buf);
	return EXCEPTION_EXECUTE_HANDLER;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

	// Modo --replace (self-update) tem prioridade sobre tudo.
	if (SelfUpdater::HandleReplaceMode(argc, argv))
	{
		if (argv) LocalFree(argv);
		return 0;
	}

	AppConfig cfg;
	cfg.Load(argc, argv);
	if (argv) LocalFree(argv);

	Localization::Initialize(cfg.language);
	SetUnhandledExceptionFilter(CrashHandler);

	HANDLE mutex = CreateMutexW(nullptr, FALSE, L"PWLauncherExist");
	if (GetLastError() == ERROR_ALREADY_EXISTS)
	{
		HWND h = FindWindowW(L"PWLauncherClass", nullptr);
		if (h) { ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); }
		return 0;
	}

	MakeDirs(cfg.elementDir);
	Log::Init(cfg.elementLogFile);
	Log::Write(L"===== launcher start =====");
	Log::Write(L"server base: " + cfg.serverBase);
	Log::Write(L"install dir: " + cfg.elementDir);

	auto behavior = [&cfg]() -> bool
	{
		// Pre-requisitos (VC++/DirectX). Nao bloqueia a atualizacao.
		try
		{
			PrerequisiteService prereq(cfg.elementDir, [](const std::wstring& m) { Log::Write(m); });
			if (!prereq.EnsureInstalled())
				Log::Write(L"alguns pre-requisitos falharam; continuando");
		}
		catch (...) {}

		// Icones de guilda (surfaces.pck). Opcional, controlado pelo config.json.
		try
		{
			GuildIconsUpdater guild(cfg, [](const std::wstring& m) { Log::Write(m); });
			guild.Run();
		}
		catch (...) {}

		// Self-update adaptado ao servidor classico. Se agendar, devolve true.
		try
		{
			SelfUpdater su(cfg, [](const std::wstring& m) { Log::Write(m); });
			std::wstring remote;
			if (su.NeedsSelfUpdate(remote) && su.PerformSelfUpdate(remote))
				return true;
		}
		catch (...) {}
		return false;
	};

	if (cfg.silent)
	{
		behavior();
		UpdateEngine engine(cfg);
		engine.Run();
		UpdateStatus s = engine.Snapshot();
		if (cfg.autoClose && s.canPlay)
		{
			std::wstring err;
			engine.LaunchGame(err);
		}
		return s.failed ? 1 : 0;
	}

	Gdiplus::GdiplusStartupInput gdiInput;
	ULONG_PTR gdiToken = 0;
	Gdiplus::GdiplusStartup(&gdiToken, &gdiInput, nullptr);

	UpdateEngine engine(cfg);
	LauncherWindow win(cfg, engine);

	if (!win.Create(hInst))
	{
		Gdiplus::GdiplusShutdown(gdiToken);
		return 1;
	}

	engine.SetOnChange([&win]() {
		PostMessageW(win.Handle(), WM_APP + 1, 0, 0);
	});

	std::thread worker([&]() {
		if (behavior())
		{
			PostMessageW(win.Handle(), WM_CLOSE, 0, 0);
			return;
		}
		engine.Run();
	});

	win.SetOnPlay([&]() {
		std::wstring err;
		if (!engine.LaunchGame(err))
			win.ShowError(err);
		else if (cfg.autoClose || cfg.closeLauncherOnStart)
			PostMessageW(win.Handle(), WM_CLOSE, 0, 0);
	});

	win.SetOnLoginAccount([&](int idx) {
		AccountStore& store = win.Accounts();
		if (idx < 0 || (size_t)idx >= store.Count()) return;
		const Account& a = store.Get((size_t)idx);
		std::wstring err;
		if (!engine.LaunchGameWithLogin(a.login, a.password, a.role, err))
			win.ShowError(err);
		else if (cfg.autoClose || cfg.closeLauncherOnStart)
			PostMessageW(win.Handle(), WM_CLOSE, 0, 0);
	});

	win.SetOnClose([&engine]() {
		engine.RequestStop();
	});

	// Re-executa o motor (Verificar arquivos / Check Updates).
	std::thread rerun;
	auto startRun = [&](bool verify) {
		if (engine.Running()) return;
		if (rerun.joinable()) rerun.join();
		cfg.verifyMode = verify;
		cfg.fullCheck = verify;
		engine.ResetForRun();
		rerun = std::thread([&]() { engine.Run(); });
	};
	win.SetOnVerify([&]() { startRun(true); });
	win.SetOnCheckUpdates([&]() { startRun(false); });

	// Carrega noticias (news.json) em segundo plano.
	std::thread newsThread([&]() {
		NewsService news(cfg.serverBase, [](const std::wstring& m) { Log::Write(m); });
		win.SetNews(news.LoadTop(3));
	});

	MSG msg;
	while (GetMessageW(&msg, nullptr, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	engine.RequestStop();
	if (worker.joinable()) worker.join();
	if (rerun.joinable()) rerun.join();
	if (newsThread.joinable()) newsThread.join();

	Gdiplus::GdiplusShutdown(gdiToken);
	Log::Write(L"===== launcher exit =====");
	return 0;
}
