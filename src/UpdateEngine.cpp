#include "UpdateEngine.h"
#include "Common.h"
#include "BuildConfig.h"
#include "Md5.h"
#include "Compression.h"
#include "Signature.h"
#include "Base64.h"
#include "HttpClient.h"
#include "Json.h"
#include "Localization.h"
#include "Log.h"
#include <shellapi.h>
#include <tlhelp32.h>
#include <vector>
#include <cstdio>
#include <cstring>

namespace el {

static bool IEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
	return true;
}

UpdateEngine::UpdateEngine(AppConfig& cfg)
	: m_cfg(cfg), m_stop(false), m_running(false), m_totalEntries(0), m_entriesDone(0)
{
	InitializeCriticalSection(&m_cs);
	SetCompressionLevel(cfg.zlibLevel);
	m_elementServerName = cfg.serverBase + L"element/";
	m_elementServerPath = m_elementServerName + L"element/";
}

void UpdateEngine::ResetForRun()
{
	m_stop = false;
	EnterCriticalSection(&m_cs);
	m_status = UpdateStatus();
	m_rateSampleBytes = 0;
	m_rateSampleTick = 0;
	LeaveCriticalSection(&m_cs);
	Notify();
}

UpdateStatus UpdateEngine::Snapshot()
{
	EnterCriticalSection(&m_cs);
	UpdateStatus s = m_status;
	LeaveCriticalSection(&m_cs);
	return s;
}

void UpdateEngine::SetPhase(UpdatePhase p, const std::wstring& msg)
{
	EnterCriticalSection(&m_cs);
	m_status.phase = p;
	if (!msg.empty()) m_status.message = msg;
	LeaveCriticalSection(&m_cs);
	Notify();
}

void UpdateEngine::SetCurrentFile(const std::wstring& f)
{
	EnterCriticalSection(&m_cs);
	m_status.currentFile = f;
	LeaveCriticalSection(&m_cs);
	Notify();
}

void UpdateEngine::Notify()
{
	EnterCriticalSection(&m_cs);
	m_status.revision++;
	LeaveCriticalSection(&m_cs);
	if (m_onChange) m_onChange();
}

int UpdateEngine::ReadIntFile(const std::wstring& path, int def)
{
	FILE* f = _wfopen(path.c_str(), L"rb");
	if (!f) return def;
	int v = def;
	if (fscanf(f, "%d", &v) != 1) v = def;
	fclose(f);
	return v;
}

void UpdateEngine::WriteVersionFile(int version)
{
	EnsureWritablePath(m_cfg.elementVersionFile);
	FILE* f = _wfopen(m_cfg.elementVersionFile.c_str(), L"wb");
	if (f)
	{
		fprintf(f, "%d\n", version);
		fclose(f);
	}
}

bool UpdateEngine::IsGameClientRunning()
{
	static const wchar_t* names[] = {
		L"elementclient.exe", L"elementclient_64.exe", L"elementclient_x64.exe"
	};
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE) return false;
	PROCESSENTRY32W pe;
	pe.dwSize = sizeof(pe);
	bool running = false;
	if (Process32FirstW(snap, &pe))
	{
		do
		{
			for (const wchar_t* n : names)
				if (_wcsicmp(pe.szExeFile, n) == 0) { running = true; break; }
			if (running) break;
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return running;
}

void UpdateEngine::UpdateTransferRate(uint64_t done, uint64_t total)
{
	ULONGLONG now = GetTickCount64();
	if (m_rateSampleTick == 0 || now - m_rateSampleTick >= 500)
	{
		uint64_t delta = (done >= m_rateSampleBytes) ? done - m_rateSampleBytes : 0;
		ULONGLONG dt = now - m_rateSampleTick;
		if (m_rateSampleTick != 0 && dt > 0)
		{
			double bps = (double)delta * 1000.0 / (double)dt;
			EnterCriticalSection(&m_cs);
			m_status.speedText = FormatBytes((uint64_t)bps) + L"/s";
			m_status.etaSeconds = (bps > 1.0 && total > done) ? (int)((total - done) / bps) : -1;
			LeaveCriticalSection(&m_cs);
		}
		m_rateSampleBytes = done;
		m_rateSampleTick = now;
	}
}

void UpdateEngine::Fail(const std::wstring& msg)
{
	Log::Write(L"ERRO: " + msg);
	EnterCriticalSection(&m_cs);
	m_status.failed = true;
	m_status.phase = UpdatePhase::Error;
	m_status.message = msg;
	LeaveCriticalSection(&m_cs);
	Notify();
}

bool UpdateEngine::CheckPid(std::wstring& err)
{
	if (m_cfg.serverBase.empty())
	{
		err = L"Servidor de atualizacao nao configurado";
		return false;
	}
	SetCurrentFile(L"");
	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);
	std::wstring pidUrl = m_cfg.serverBase + L"info/pid";
	DWORD status = 0;
	bool ok = http.Download(pidUrl, m_cfg.tempFile, nullptr, 0, &status);
	if (!ok)
	{
		Log::Write(L"info/pid nao disponivel, continuando");
		return true;
	}
	int newPid = ReadIntFile(m_cfg.tempFile, 0);
	DeleteFileW(m_cfg.tempFile.c_str());
	if (newPid == 0)
	{
		if (kStrictPidCheck)
		{
			err = L"Servidor de atualizacao ocupado";
			return false;
		}
		Log::Write(L"pid do servidor = 0, continuando");
		return true;
	}
	if (newPid != m_cfg.pid)
	{
		Log::Write(Format(L"pid do servidor (%d) difere do local (%d)", newPid, m_cfg.pid));
		if (kStrictPidCheck)
		{
			err = L"Numero de serie do servidor invalido";
			return false;
		}
	}
	return true;
}

bool UpdateEngine::GetVersion(int& outVersion, std::wstring& err)
{
	outVersion = 0;
	EnsureParentDir(m_cfg.elementNewVersionFile);

	const wchar_t* subs[] = { L"element/", L"patcher/", L"" };
	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);
	std::wstring diag;

	for (const wchar_t* sub : subs)
	{
		std::wstring serverName = m_cfg.serverBase + sub;
		std::wstring serverPath = serverName + sub;
		std::wstring url = serverName + L"version";

		DWORD status = 0;
		Log::Write(L"GET " + url);
		bool ok = http.Download(url, m_cfg.elementNewVersionFile, nullptr, 0, &status);
		if (!ok)
		{
			Log::Write(Format(L"falha ao baixar versao (HTTP %u): %s", status, url.c_str()));
			diag += url + (status ? Format(L"  -> HTTP %u\r\n", status) : L"  -> sem conexao\r\n");
			continue;
		}
		int v = ReadIntFile(m_cfg.elementNewVersionFile, 0);
		DeleteFileW(m_cfg.elementNewVersionFile.c_str());
		if (v <= 0)
		{
			Log::Write(Format(L"versao invalida em %s", url.c_str()));
			diag += url + L"  -> conteudo invalido\r\n";
			continue;
		}
		m_elementServerName = serverName;
		m_elementServerPath = serverPath;
		outVersion = v;
		Log::Write(Format(L"layout do servidor: %s (versao %d)", sub, v));
		return true;
	}

	err = L"Falha ao obter a versao do servidor:\r\n\r\n" + diag;
	return false;
}

int UpdateEngine::ComputeIncrement(int from, int to)
{
	// O CPWCreator gera "v-<k>.inc" onde k = versaoAlvo - versaoLocal
	// (cabecalho "# <local> <alvo> <bytes>"). Ex.: de 146 para 147 -> v-1.inc.
	int k = to - from;
	return k < 1 ? 1 : k;
}

bool UpdateEngine::DownloadList(const std::wstring& url, const std::wstring& dest, std::wstring& err)
{
	EnsureParentDir(dest);
	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);
	DWORD status = 0;
	Log::Write(L"GET " + url);
	if (!http.Download(url, dest, nullptr, 0, &status))
	{
		Log::Write(Format(L"falha ao baixar lista (HTTP %u): %s", status, url.c_str()));
		err = L"Falha ao baixar a lista de atualizacao";
		return false;
	}
	Log::Write(Format(L"lista baixada (HTTP %u): %s", status, url.c_str()));
	return true;
}

void UpdateEngine::RemoveLocal(const std::wstring& path)
{
	SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileW(path.c_str());

	std::wstring dir = path;
	size_t pos = dir.find_last_of(L"\\/");
	while (pos != std::wstring::npos)
	{
		dir = dir.substr(0, pos);
		if (!RemoveDirectoryW(dir.c_str())) break;
		pos = dir.find_last_of(L"\\/");
	}
}

bool UpdateEngine::DownloadEntryBuffer(const std::wstring& token, const std::string& hash,
	std::vector<uint8_t>& packedOut, std::wstring& err, DWORD* httpStatus)
{
	if (httpStatus) *httpStatus = 0;
	for (int attempt = 0; attempt < m_cfg.maxRetries; ++attempt)
	{
		if (m_stop) { err = L"Cancelado"; return false; }

		HttpClient http;
		http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);

		EnterCriticalSection(&m_cs);
		m_status.fileDone = 0;
		m_status.fileTotal = 0;
		m_status.filePercent = 0;
		LeaveCriticalSection(&m_cs);

		DWORD status = 0;
		std::wstring url = m_elementServerPath + token;
		bool ok = http.Download(url, m_cfg.tempFile,
			[this](uint64_t done, uint64_t total) -> bool
			{
				EnterCriticalSection(&m_cs);
				m_status.fileDone = done;
				m_status.fileTotal = total;
				m_status.filePercent = total ? (int)(done * 100 / total) : 0;
				LeaveCriticalSection(&m_cs);
				UpdateTransferRate(done, total);
				Notify();
				return !m_stop;
			}, 0, &status);

		if (httpStatus) *httpStatus = status;

		if (ok)
		{
			std::vector<uint8_t> buf;
			HANDLE h = CreateFileW(m_cfg.tempFile.c_str(), GENERIC_READ, FILE_SHARE_READ,
				nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h != INVALID_HANDLE_VALUE)
			{
				LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
				buf.resize((size_t)sz.QuadPart);
				DWORD read = 0, total = 0;
				while (total < buf.size() &&
					ReadFile(h, buf.data() + total, (DWORD)(buf.size() - total), &read, nullptr) && read > 0)
					total += read;
				buf.resize(total);
				CloseHandle(h);
			}
			DeleteFileW(m_cfg.tempFile.c_str());

			std::string md = Md5::Buffer(buf.data(), buf.size());
			if (IEquals(md, hash))
			{
				packedOut = std::move(buf);
				return true;
			}
			err = L"Verificacao MD5 falhou: " + token;
		}
		else
		{
			err = (status == 404) ? (L"404: " + token) : (L"Falha no download: " + token);
		}

		if (m_stop) { err = L"Cancelado"; return false; }
		if (status == 404) return false;   // nao insiste em arquivo ausente
		Sleep(m_cfg.reconnectMs);
	}
	return false;
}

static bool WriteAll(const std::wstring& path, const uint8_t* data, size_t len)
{
	EnsureWritablePath(path);
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	size_t off = 0;
	bool ok = true;
	while (off < len)
	{
		if (!WriteFile(h, data + off, (DWORD)(len - off), &written, nullptr) || written == 0)
		{
			ok = false;
			break;
		}
		off += written;
	}
	CloseHandle(h);
	return ok;
}

bool UpdateEngine::DownloadEntry(const std::wstring& token, const std::string& hash,
	const std::wstring& destPath, std::wstring& err, DWORD* httpStatus)
{
	std::vector<uint8_t> packed;
	if (!DownloadEntryBuffer(token, hash, packed, err, httpStatus))
		return false;

	std::vector<uint8_t> raw;
	if (!UnpackBuffer(packed.data(), packed.size(), raw))
	{
		err = L"Falha ao descompactar " + token;
		return false;
	}
	EnsureParentDir(destPath);
	if (!WriteAll(destPath, raw.data(), raw.size()))
	{
		err = L"Falha ao gravar " + destPath;
		return false;
	}
	return true;
}

bool UpdateEngine::ApplyList(const std::wstring& listPath, bool fullList, int newVersion, std::wstring& err)
{
	FILE* f = _wfopen(listPath.c_str(), L"rb");
	if (!f)
	{
		err = L"Nao foi possivel abrir a lista de atualizacao";
		return false;
	}

	std::vector<std::string> lines;
	char buf[2048];
	while (fgets(buf, sizeof(buf), f))
	{
		if (buf[0] == '#') continue;
		if (strncmp(buf, "-----BEGIN ELEMENT SIGNATURE-----", 33) == 0) break;
		std::string s(buf);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
		if (s.empty()) continue;
		lines.push_back(s);
	}
	fclose(f);

	EnterCriticalSection(&m_cs);
	m_totalEntries = lines.size();
	m_entriesDone = 0;
	m_status.totalPercent = 0;
	LeaveCriticalSection(&m_cs);

	Log::Write(Format(L"lista de atualizacao: %d arquivos", (int)lines.size()));
	int skipped404 = 0;
	uint64_t statOk = 0, statRepaired = 0, statChecked = 0;

	for (size_t i = 0; i < lines.size(); ++i)
	{
		if (m_stop) { err = L"Cancelado"; return false; }

		char md5Tok[512] = { 0 };
		char fileTok[1536] = { 0 };
		if (sscanf(lines[i].c_str(), "%511s %1535s", md5Tok, fileTok) < 2)
			continue;

		std::string outPrefix;
		std::wstring path = DecodeFileName(fileTok, outPrefix);
		for (wchar_t& c : path) if (c == L'/') c = L'\\';
		while (path.size() >= 2 && path[0] == L'.' && path[1] == L'\\') path = path.substr(2);
		std::string ansiPath = Narrow(path, 936);
		++statChecked;

		// O manifesto agrupa pastas: quando a pasta repete, a linha traz so o nome.
		// O token de download precisa do prefixo (pasta) reconstruido (g_prefix),
		// ex.: "ZGF0YQ==/ZWxlbWVudHMuZGF0YQ==" a partir de "./ZGF0YQ==/" + nome.
		std::string fileTokStr(fileTok);
		std::string fullTok = (!fileTokStr.empty() && fileTokStr[0] == '/')
			? ("." + fileTokStr)
			: (outPrefix + fileTokStr);
		std::wstring localPath = JoinPath(m_cfg.elementDir, path);
		pck::PckPackage* pkg = m_pckMgr.Resolve(path);

		std::wstring baseName = path;
		size_t slash = baseName.find_last_of(L"\\/");
		if (slash != std::wstring::npos) baseName = baseName.substr(slash + 1);

		bool deleted = (md5Tok[0] == '-');
		if (deleted)
		{
			SetCurrentFile(L"Removendo " + baseName);
			if (pkg) pkg->Remove(ansiPath);
			else RemoveLocal(localPath);
		}
		else
		{
			std::string hash = fullList ? std::string(md5Tok) : std::string(md5Tok + 1);
			bool need = true;
			if (m_cfg.verifyLocal)
			{
				std::string localMd;
				if (pkg)
				{
					std::vector<uint8_t> pk;
					if (pkg->GetPacked(ansiPath, pk))
						localMd = Md5::Buffer(pk.data(), pk.size());
				}
				else if (FileExists(localPath))
					PackedMd5OfFile(localPath, localMd);
				if (!localMd.empty() && IEquals(localMd, hash))
					need = false;
			}
			if (need)
			{
				SetCurrentFile(L"Baixando " + baseName);
				DWORD status = 0;
				bool got = true;
				if (pkg)
				{
					std::vector<uint8_t> buf;
					got = DownloadEntryBuffer(Widen(fullTok, CP_ACP), hash, buf, err, &status);
					if (got && !pkg->AddOrReplacePacked(ansiPath, buf.data(), buf.size()))
					{
						err = L"Falha ao gravar no pacote: " + baseName;
						return false;
					}
					if (got) Log::Write(L"aplicado (pck): " + path);
				}
				else
				{
					got = DownloadEntry(Widen(fullTok, CP_ACP), hash, localPath, err, &status);
					if (got) Log::Write(L"aplicado: " + localPath);
				}

				if (!got)
				{
					if (kSkipMissingFiles && status == 404)
					{
						++skipped404;
						Log::Write(L"arquivo ausente no servidor (404), ignorando: " + path);
					}
					else
						return false;
				}
				else
				{
					++statRepaired;
				}
			}
			else
			{
				++statOk;
				EnterCriticalSection(&m_cs);
				m_status.filePercent = 100;
				LeaveCriticalSection(&m_cs);
				Notify();
			}
		}

		EnterCriticalSection(&m_cs);
		m_entriesDone = i + 1;
		m_status.totalPercent = m_totalEntries ? (int)(m_entriesDone * 100 / m_totalEntries) : 100;
		LeaveCriticalSection(&m_cs);
		Notify();
	}

	SetCurrentFile(L"");
	Log::Write(Format(L"atualizacao concluida (v%d), %d arquivo(s) ausente(s) no servidor", newVersion, skipped404));
	if (m_cfg.verifyMode)
	{
		std::wstring rep = Localization::Format(Localization::Get(L"ui.verify_done"),
			{ std::to_wstring(statOk), std::to_wstring(statRepaired), std::to_wstring(skipped404) });
		Log::Write(L"[verify] " + rep);
		EnterCriticalSection(&m_cs);
		m_status.message = rep;
		LeaveCriticalSection(&m_cs);
		Notify();
	}
	WriteVersionFile(newVersion);
	DeleteFileW(m_cfg.elementUpdateListFile.c_str());
	DeleteFileW(m_cfg.elementUpdateStateFile.c_str());
	return true;
}

bool UpdateEngine::RunFullCheck(int newVersion, std::wstring& err)
{
	SetPhase(UpdatePhase::DownloadingList, L"Atualizando lista de arquivos...");
	std::wstring url = m_elementServerName + L"files.md5";
	if (!DownloadList(url, m_cfg.elementFullListFile, err))
		return false;

	SetPhase(UpdatePhase::Verifying, L"Verificando lista...");
	if (m_cfg.verifySignature && !VerifySignedFile(m_cfg.elementFullListFile))
	{
		err = L"Assinatura da lista invalida";
		return false;
	}
	return ApplyList(m_cfg.elementFullListFile, true, newVersion, err);
}

bool UpdateEngine::RunIncremental(int newVersion, int oldVersion, std::wstring& err)
{
	int k = ComputeIncrement(oldVersion, newVersion);
	std::wstring url = m_elementServerName + Format(L"v-%d.inc", k);
	std::wstring incPath = m_cfg.elementUpdateListFile;
	SetPhase(UpdatePhase::DownloadingList, L"Baixando atualizacao...");

	std::wstring tmpErr;
	if (!DownloadList(url, incPath, tmpErr))
	{
		Log::Write(L"Incremental indisponivel, usando lista completa");
		return RunFullCheck(newVersion, err);
	}

	SetPhase(UpdatePhase::Verifying, L"Verificando atualizacao...");
	if (m_cfg.verifySignature && !VerifySignedFile(incPath))
	{
		Log::Write(L"Assinatura incremental invalida, usando lista completa");
		return RunFullCheck(newVersion, err);
	}
	return ApplyList(incPath, false, newVersion, err);
}

void UpdateEngine::Run()
{
	struct RunningGuard { std::atomic<bool>& f; RunningGuard(std::atomic<bool>& x) : f(x) { f = true; } ~RunningGuard() { f = false; } } guard(m_running);
	std::wstring err;

	// Pre-requisitos de comportamento (iguais ao launcher de referencia):
	// espaco em disco e cliente do jogo fechado.
	{
		std::wstring diskDir = m_cfg.elementDir.empty() ? m_cfg.exeDir : m_cfg.elementDir;
		int freeMB = GetFreeDiskSpaceMB(diskDir);
		if (freeMB >= 0 && freeMB < 3072)
		{
			Log::Write(Format(L"espaco em disco insuficiente: %d MB", freeMB));
			Fail(Localization::Get(L"ui.insufficient_disk_space"));
			return;
		}
	}
	if (IsGameClientRunning())
	{
		Log::Write(L"cliente do jogo esta em execucao; abortando atualizacao");
		Fail(Localization::Get(L"ui.close_game_before_update"));
		return;
	}

	m_pckMgr.Init(m_cfg.elementDir, m_cfg.usePack);

	JsonResult jr = RunJsonUpdate(err);
	if (jr == JsonResult::Ok)
		return;
	if (jr == JsonResult::Failed)
	{
		Fail(err.empty() ? L"Falha ao atualizar" : err);
		return;
	}

	SetPhase(UpdatePhase::Connecting, L"Conectando...");
	if (!CheckPid(err)) { Fail(err); return; }

	SetPhase(UpdatePhase::CheckingVersion, L"Verificando versao...");
	int newVersion = 0;
	if (!GetVersion(newVersion, err)) { Fail(err); return; }

	int oldVersion = ReadIntFile(m_cfg.elementVersionFile, 0);

	EnterCriticalSection(&m_cs);
	m_status.versionText = Format(L"Versao atual: %d", oldVersion);
	LeaveCriticalSection(&m_cs);
	Notify();

	bool ok = true;
	if (m_cfg.verifyMode)
		SetPhase(UpdatePhase::Verifying, Localization::Get(L"ui.verifying"));
	if (newVersion == oldVersion && !m_cfg.fullCheck)
	{
		ok = true;
	}
	else if (m_cfg.fullCheck || oldVersion < 1 || newVersion < oldVersion)
	{
		ok = RunFullCheck(newVersion, err);
	}
	else
	{
		ok = RunIncremental(newVersion, oldVersion, err);
	}

	if (m_stop)
	{
		SetPhase(UpdatePhase::Idle, L"Cancelado");
		return;
	}

	if (ok && !m_pckMgr.FlushAll())
	{
		ok = false;
		err = L"Falha ao salvar pacotes (.pck)";
	}

	EnterCriticalSection(&m_cs);
	if (ok)
	{
		m_status.phase = UpdatePhase::Done;
		m_status.canPlay = true;
		m_status.filePercent = 100;
		m_status.totalPercent = 100;
		m_status.versionText = Format(L"Versao atual: %d", newVersion);
		m_status.message = L"Jogo atualizado";
		LeaveCriticalSection(&m_cs);
		Notify();
	}
	else
	{
		LeaveCriticalSection(&m_cs);
		Fail(err.empty() ? L"Falha ao atualizar" : err);
	}
}

static bool ReadAllTextA(const std::wstring& path, std::string& out)
{
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	LARGE_INTEGER sz; GetFileSizeEx(h, &sz);
	out.resize((size_t)sz.QuadPart);
	DWORD read = 0, total = 0;
	while (total < out.size() && ReadFile(h, out.data() + total, (DWORD)(out.size() - total), &read, nullptr) && read > 0)
		total += read;
	out.resize(total);
	CloseHandle(h);
	return true;
}

UpdateEngine::JsonResult UpdateEngine::RunJsonUpdate(std::wstring& err)
{
	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);

	std::wstring vurl = m_cfg.serverBase + L"versions.json";
	DWORD status = 0;
	Log::Write(L"GET " + vurl);
	if (!http.Download(vurl, m_cfg.tempFile, nullptr, 0, &status))
	{
		Log::Write(Format(L"versions.json indisponivel (HTTP %u)", status));
		return JsonResult::NotAvailable;
	}
	std::string vtext;
	ReadAllTextA(m_cfg.tempFile, vtext);
	DeleteFileW(m_cfg.tempFile.c_str());

	json::Value vroot; std::string jerr;
	if (!json::Parse(vtext, vroot, jerr))
	{
		err = L"versions.json invalido: " + Widen(jerr, CP_ACP);
		return JsonResult::Failed;
	}
	int latest = (int)vroot.GetInt("latestVersion", 0);
	if (latest <= 0)
	{
		err = L"versions.json sem latestVersion";
		return JsonResult::Failed;
	}

	int local = ReadIntFile(m_cfg.launcherVersionFile, 0);
	EnterCriticalSection(&m_cs);
	m_status.versionText = Format(L"Versao atual: %d", local);
	LeaveCriticalSection(&m_cs);
	Notify();

	if (latest == local && !m_cfg.fullCheck)
	{
		EnterCriticalSection(&m_cs);
		m_status.phase = UpdatePhase::Done;
		m_status.canPlay = true;
		m_status.filePercent = 100;
		m_status.totalPercent = 100;
		m_status.message = L"Jogo atualizado";
		LeaveCriticalSection(&m_cs);
		Notify();
		return JsonResult::Ok;
	}

	SetPhase(UpdatePhase::DownloadingList, L"Baixando manifest...");
	std::wstring murl = m_cfg.serverBase + L"element/manifest.json";
	Log::Write(L"GET " + murl);
	if (!http.Download(murl, m_cfg.elementFullListFile, nullptr, 0, &status))
	{
		err = L"Falha ao baixar element/manifest.json";
		return JsonResult::Failed;
	}
	std::string mtext;
	ReadAllTextA(m_cfg.elementFullListFile, mtext);

	json::Value mroot;
	if (!json::Parse(mtext, mroot, jerr))
	{
		err = L"manifest.json invalido: " + Widen(jerr, CP_ACP);
		return JsonResult::Failed;
	}
	const json::Value* files = mroot.Find("files");
	if (!files || !files->IsArray())
	{
		err = L"manifest.json sem lista files[]";
		return JsonResult::Failed;
	}

	uint64_t totalBytes = 0;
	for (const json::Value& f : files->arr)
		totalBytes += (uint64_t)f.GetInt("size", 0);

	EnterCriticalSection(&m_cs);
	m_totalEntries = files->arr.size();
	m_entriesDone = 0;
	m_status.totalPercent = 0;
	LeaveCriticalSection(&m_cs);
	Notify();

	uint64_t doneBytes = 0;
	for (size_t i = 0; i < files->arr.size(); ++i)
	{
		if (m_stop) { err = L"Cancelado"; return JsonResult::Failed; }

		const json::Value& f = files->arr[i];
		std::string path = f.GetString("path");
		std::string p64 = f.GetString("pathBase64");
		std::string hash = f.GetString("hash");
		uint64_t size = (uint64_t)f.GetInt("size", 0);

		std::wstring wpath = Widen(path, CP_UTF8);
		for (wchar_t& c : wpath) if (c == L'/') c = L'\\';
		while (wpath.size() >= 2 && wpath[0] == L'.' && wpath[1] == L'\\') wpath = wpath.substr(2);
		std::wstring localPath = JoinPath(m_cfg.elementDir, wpath);

		std::wstring baseName = wpath;
		size_t slash = baseName.find_last_of(L"\\/");
		if (slash != std::wstring::npos) baseName = baseName.substr(slash + 1);
		SetCurrentFile(L"Baixando " + baseName);

		bool need = true;
		if (m_cfg.verifyLocal && FileExists(localPath))
		{
			std::string md;
			if (Md5::File(localPath, md) && IEquals(md, hash))
				need = false;
		}

		if (need)
		{
			std::wstring url = m_cfg.serverBase + L"element/element/" + Widen(p64, CP_ACP);
			EnterCriticalSection(&m_cs);
			m_status.fileDone = 0;
			m_status.fileTotal = size;
			m_status.filePercent = 0;
			LeaveCriticalSection(&m_cs);
			Notify();
			if (!DownloadJsonFile(url, localPath, hash, size, err))
				return JsonResult::Failed;
		}

		doneBytes += size;
		EnterCriticalSection(&m_cs);
		m_entriesDone = i + 1;
		m_status.totalPercent = totalBytes ? (int)(doneBytes * 100 / totalBytes) : 100;
		m_status.filePercent = 100;
		LeaveCriticalSection(&m_cs);
		Notify();
	}

	SetCurrentFile(L"");
	{
		EnsureWritablePath(m_cfg.launcherVersionFile);
		FILE* fv = _wfopen(m_cfg.launcherVersionFile.c_str(), L"wb");
		if (fv) { fprintf(fv, "%d\n", latest); fclose(fv); }
	}

	EnterCriticalSection(&m_cs);
	m_status.phase = UpdatePhase::Done;
	m_status.canPlay = true;
	m_status.filePercent = 100;
	m_status.totalPercent = 100;
	m_status.versionText = Format(L"Versao atual: %d", latest);
	m_status.message = L"Jogo atualizado";
	LeaveCriticalSection(&m_cs);
	Notify();
	return JsonResult::Ok;
}

bool UpdateEngine::DownloadJsonFile(const std::wstring& url, const std::wstring& dest,
	const std::string& hash, uint64_t expectedSize, std::wstring& err)
{
	EnsureParentDir(dest);
	std::wstring tmp = dest + L".impw_tmp";
	DeleteFileW(tmp.c_str());

	HttpClient http;
	http.SetTimeouts(m_cfg.connectTimeoutMs, m_cfg.receiveTimeoutMs);
	uint64_t rawSize = 0;
	DWORD status = 0;
	bool ok = http.DownloadInflate(url, tmp, &rawSize,
		[this](uint64_t done, uint64_t total) -> bool
		{
			EnterCriticalSection(&m_cs);
			m_status.fileDone = done;
			if (total) m_status.fileTotal = total;
			m_status.filePercent = total ? (int)(done * 100 / total) : 0;
			LeaveCriticalSection(&m_cs);
			UpdateTransferRate(done, total);
			Notify();
			return !m_stop;
		}, &status);

	if (!ok)
	{
		DeleteFileW(tmp.c_str());
		err = Format(L"Falha ao baixar (HTTP %u): %s", status, url.c_str());
		return false;
	}

	std::string md;
	if (!Md5::File(tmp, md) || !IEquals(md, hash))
	{
		DeleteFileW(tmp.c_str());
		err = L"MD5 invalido apos baixar: " + url;
		return false;
	}

	SetFileAttributesW(dest.c_str(), FILE_ATTRIBUTE_NORMAL);
	DeleteFileW(dest.c_str());
	if (!MoveFileExW(tmp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING))
	{
		DeleteFileW(tmp.c_str());
		err = L"Falha ao gravar " + dest;
		return false;
	}
	return true;
}

static bool Is64BitOS()
{
	SYSTEM_INFO si;
	GetNativeSystemInfo(&si);
	return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
		|| si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_IA64;
}

bool UpdateEngine::LaunchGame(std::wstring& errorOut)
{
	return LaunchGameInternal(std::wstring(), errorOut);
}

// Auto-login: o elementclient entende startbypatcher game:cpw console:1 nocheck
// user:<login> pwd:<senha> role:<personagem> (role e opcional).
bool UpdateEngine::LaunchGameWithLogin(const std::wstring& login, const std::wstring& password,
	const std::wstring& role, std::wstring& errorOut)
{
	std::wstring extra = L"startbypatcher game:cpw console:1 nocheck";
	if (!login.empty())    extra += L" user:" + login;
	if (!password.empty()) extra += L" pwd:" + password;
	if (!role.empty())     extra += L" role:" + role;
	return LaunchGameInternal(extra, errorOut);
}

bool UpdateEngine::LaunchGameInternal(const std::wstring& extraArgs, std::wstring& errorOut)
{
	bool use64 = m_cfg.use64BitOn64OS && Is64BitOS();
	std::wstring exeName = use64 ? m_cfg.exeX64 : m_cfg.exeX86;
	if (exeName.empty()) exeName = use64 ? L"x64\\elementclient_64.exe" : L"elementclient.exe";
	std::wstring args = use64 ? m_cfg.argsX64 : m_cfg.argsX86;
	if (!extraArgs.empty())
		args = args.empty() ? extraArgs : (args + L" " + extraArgs);

	std::wstring path = JoinPath(m_cfg.elementDir, exeName);
	if (!FileExists(path))
	{
		errorOut = Localization::Get(L"ui.game_not_found") + L"\r\n" + path;
		return false;
	}

	std::wstring workDir;
	if (!m_cfg.workingDir.empty())
		workDir = JoinPath(m_cfg.elementDir, m_cfg.workingDir);
	else
	{
		workDir = m_cfg.elementDir;
	}
	if (!DirExists(workDir)) workDir = m_cfg.elementDir;

	SHELLEXECUTEINFOW sei = { 0 };
	sei.cbSize = sizeof(sei);
	sei.fMask = SEE_MASK_NOCLOSEPROCESS;
	sei.lpVerb = m_cfg.runAsAdmin ? L"runas" : L"open";
	sei.lpFile = path.c_str();
	sei.lpParameters = args.empty() ? nullptr : args.c_str();
	sei.lpDirectory = workDir.c_str();
	sei.nShow = SW_SHOWNORMAL;

	if (!ShellExecuteExW(&sei))
	{
		DWORD e = GetLastError();
		if (e == ERROR_CANCELLED) errorOut = Localization::Get(L"ui.game_start_error_access_denied");
		else errorOut = Localization::Get(L"ui.game_start_failed");
		return false;
	}
	if (sei.hProcess) CloseHandle(sei.hProcess);
	Log::Write(L"jogo iniciado: " + path);
	return true;
}

}
