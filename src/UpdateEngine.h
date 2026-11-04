#pragma once
#include "Config.h"
#include "PckPackage.h"
#include <windows.h>
#include <string>
#include <functional>
#include <atomic>
#include <cstdint>

namespace el {

enum class UpdatePhase
{
	Idle,
	Connecting,
	CheckingVersion,
	DownloadingList,
	Verifying,
	Downloading,
	Deploying,
	Done,
	Error
};

struct UpdateStatus
{
	UpdatePhase phase = UpdatePhase::Idle;
	std::wstring message;
	std::wstring currentFile;
	std::wstring versionText;
	std::wstring speedText;
	int filePercent = 0;
	int totalPercent = 0;
	uint64_t fileDone = 0;
	uint64_t fileTotal = 0;
	int etaSeconds = -1;
	bool canPlay = false;
	bool failed = false;
	int errorCode = 0;
	uint32_t revision = 0;
};

class UpdateEngine
{
public:
	explicit UpdateEngine(AppConfig& cfg);

	void Run();
	void RequestStop() { m_stop = true; }
	void ResetForRun();
	bool Running() const { return m_running.load(); }

	UpdateStatus Snapshot();
	void SetOnChange(std::function<void()> cb) { m_onChange = cb; }

	bool LaunchGame(std::wstring& errorOut);
	bool LaunchGameWithLogin(const std::wstring& login, const std::wstring& password,
		const std::wstring& role, std::wstring& errorOut);

private:
	bool LaunchGameInternal(const std::wstring& extraArgs, std::wstring& errorOut);
	void SetPhase(UpdatePhase p, const std::wstring& msg);
	void SetCurrentFile(const std::wstring& f);
	void Notify();

	bool CheckPid(std::wstring& err);
	bool GetVersion(int& outVersion, std::wstring& err);
	bool DownloadList(const std::wstring& url, const std::wstring& dest, std::wstring& err);
	int  ComputeIncrement(int from, int to);
	bool RunFullCheck(int newVersion, std::wstring& err);
	bool RunIncremental(int newVersion, int oldVersion, std::wstring& err);
	bool ApplyList(const std::wstring& listPath, bool fullList, int newVersion, std::wstring& err);

	bool DownloadEntry(const std::wstring& token, const std::string& hash,
		const std::wstring& destPath, std::wstring& err, DWORD* httpStatus = nullptr);
	bool DownloadEntryBuffer(const std::wstring& token, const std::string& hash,
		std::vector<uint8_t>& packedOut, std::wstring& err, DWORD* httpStatus = nullptr);

	enum class JsonResult { Ok, NotAvailable, Failed };
	JsonResult RunJsonUpdate(std::wstring& err);
	bool DownloadJsonFile(const std::wstring& url, const std::wstring& dest,
		const std::string& hash, uint64_t expectedSize, std::wstring& err);
	void RemoveLocal(const std::wstring& path);
	void WriteVersionFile(int version);
	int  ReadIntFile(const std::wstring& path, int def);

	bool IsGameClientRunning();
	void UpdateTransferRate(uint64_t done, uint64_t total);
	void Fail(const std::wstring& msg);

	AppConfig& m_cfg;
	std::atomic<bool> m_stop;
	std::atomic<bool> m_running;

	CRITICAL_SECTION m_cs;
	UpdateStatus m_status;
	std::function<void()> m_onChange;

	std::wstring m_elementServerName;
	std::wstring m_elementServerPath;
	pck::PckManager m_pckMgr;
	uint64_t m_totalEntries;
	uint64_t m_entriesDone;

	uint64_t m_rateSampleBytes = 0;
	ULONGLONG m_rateSampleTick = 0;
	int m_rateLast = -1;
};

}
