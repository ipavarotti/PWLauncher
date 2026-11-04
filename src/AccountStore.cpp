#include "AccountStore.h"
#include "Common.h"
#include "Json.h"
#include "Base64.h"
#include "Log.h"
#include <wincrypt.h>
#include <vector>
#include <cstdint>

namespace el {

// Entropia adicional: amarra os blobs ao launcher (nao podem ser trocados
// entre apps diferentes nem descriptografados sem essa string).
static const wchar_t* kEntropy = L"PWLauncher.Accounts.v1";

static bool ProtectBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
	DATA_BLOB din = { 0 }, dent = { 0 }, dout = { 0 };
	din.pbData = const_cast<BYTE*>(in.data());
	din.cbData = (DWORD)in.size();
	dent.pbData = (BYTE*)kEntropy;
	dent.cbData = (DWORD)(wcslen(kEntropy) * sizeof(wchar_t));
	if (!CryptProtectData(&din, L"PWLauncher accounts", &dent, nullptr, nullptr,
		CRYPTPROTECT_UI_FORBIDDEN, &dout))
		return false;
	out.assign(dout.pbData, dout.pbData + dout.cbData);
	LocalFree(dout.pbData);
	return true;
}

static bool UnprotectBytes(const std::vector<uint8_t>& in, std::vector<uint8_t>& out)
{
	DATA_BLOB din = { 0 }, dent = { 0 }, dout = { 0 };
	din.pbData = const_cast<BYTE*>(in.data());
	din.cbData = (DWORD)in.size();
	dent.pbData = (BYTE*)kEntropy;
	dent.cbData = (DWORD)(wcslen(kEntropy) * sizeof(wchar_t));
	if (!CryptUnprotectData(&din, nullptr, &dent, nullptr, nullptr,
		CRYPTPROTECT_UI_FORBIDDEN, &dout))
		return false;
	out.assign(dout.pbData, dout.pbData + dout.cbData);
	LocalFree(dout.pbData);
	return true;
}

static bool WriteAllBytes(const std::wstring& path, const std::vector<uint8_t>& data)
{
	EnsureWritablePath(path);
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	DWORD written = 0;
	size_t off = 0;
	bool ok = true;
	while (off < data.size())
	{
		if (!WriteFile(h, data.data() + off, (DWORD)(data.size() - off), &written, nullptr) || written == 0)
		{
			ok = false;
			break;
		}
		off += written;
	}
	CloseHandle(h);
	return ok;
}

AccountStore::AccountStore() {}

// Le os nomes de conta que o cliente ja salva em userdata\accounts.txt.
// Formato: 1a linha "true"; depois "<b64(nome UTF-16LE)> <b64(servidor)>".
static void ImportGameAccounts(const std::wstring& txtPath, std::vector<Account>& out)
{
	std::string text;
	if (!ReadFileUtf8(txtPath, text) || text.empty()) return;

	size_t pos = 0;
	bool first = true;
	while (pos <= text.size())
	{
		size_t nl = text.find('\n', pos);
		std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
		pos = (nl == std::string::npos) ? text.size() + 1 : nl + 1;

		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
		size_t b = line.find_first_not_of(" \t");
		if (b == std::string::npos) { first = false; continue; }
		line = line.substr(b);
		if (first) { first = false; continue; }   // "true"

		size_t sp = line.find_first_of(" \t");
		std::string name64 = (sp == std::string::npos) ? line : line.substr(0, sp);
		if (name64.empty()) continue;

		std::vector<unsigned char> raw = base64::Decode(name64);
		if (raw.size() < 2) continue;
		std::wstring name(reinterpret_cast<const wchar_t*>(raw.data()), raw.size() / 2);
		if (name.empty()) continue;

		bool dup = false;
		for (const Account& a : out) if (a.login == name) { dup = true; break; }
		if (!dup) { Account a; a.login = name; out.push_back(a); }
	}
}

bool AccountStore::Load(const std::wstring& elementDir)
{
	m_dir = JoinPath(elementDir, L"userdata");
	m_file = JoinPath(m_dir, L"accounts.dat");
	m_accounts.clear();

	std::vector<uint8_t> enc;
	if (!ReadFileBytes(m_file, enc) || enc.empty())
	{
		// Sem nosso arquivo: ao menos importa os nomes que o jogo ja tem.
		ImportGameAccounts(JoinPath(m_dir, L"accounts.txt"), m_accounts);
		return true;
	}

	std::vector<uint8_t> plain;
	if (!UnprotectBytes(enc, plain))
	{
		Log::Write(L"AccountStore: falha ao descriptografar accounts.dat");
		return false;
	}

	std::string text(plain.begin(), plain.end());
	json::Value root; std::string jerr;
	if (!json::Parse(text, root, jerr))
	{
		Log::Write(L"AccountStore: accounts.dat invalido: " + Widen(jerr, CP_ACP));
		return false;
	}

	const json::Value* arr = root.Find("accounts");
	if (!arr || !arr->IsArray()) return true;

	for (const json::Value& o : arr->arr)
	{
		Account a;
		a.login = Widen(o.GetString("login"), CP_UTF8);
		a.role = Widen(o.GetString("role"), CP_UTF8);

		std::string pw64 = o.GetString("password");
		if (!pw64.empty())
		{
			std::vector<unsigned char> raw = base64::Decode(pw64);
			std::vector<uint8_t> enc2(raw.begin(), raw.end());
			std::vector<uint8_t> pwPlain;
			if (UnprotectBytes(enc2, pwPlain))
				a.password = Widen(std::string(pwPlain.begin(), pwPlain.end()), CP_UTF8);
		}
		m_accounts.push_back(a);
	}

	// Mescla os nomes que o jogo ja salva (sem senha) para nao ficarem de fora.
	ImportGameAccounts(JoinPath(m_dir, L"accounts.txt"), m_accounts);
	return true;
}

bool AccountStore::Save() const
{
	json::Value root = json::MakeObject();
	json::Value arr = json::MakeArray();

	for (const Account& a : m_accounts)
	{
		std::string pw64;
		std::string pw = Narrow(a.password, CP_UTF8);
		if (!pw.empty())
		{
			std::vector<uint8_t> pwEnc;
			if (!ProtectBytes(std::vector<uint8_t>(pw.begin(), pw.end()), pwEnc))
			{
				Log::Write(L"AccountStore: falha ao criptografar a senha");
				return false;
			}
			pw64 = base64::Encode(pwEnc.data(), pwEnc.size());
		}

		json::Value o = json::MakeObject();
		json::Set(o, "login", Narrow(a.login, CP_UTF8));
		json::Set(o, "password", pw64);
		json::Set(o, "role", Narrow(a.role, CP_UTF8));
		arr.arr.push_back(o);
	}
	json::Set(root, "accounts", arr);

	std::string text = json::Write(root, true);
	std::vector<uint8_t> enc;
	if (!ProtectBytes(std::vector<uint8_t>(text.begin(), text.end()), enc))
	{
		Log::Write(L"AccountStore: falha ao criptografar o arquivo");
		return false;
	}
	MakeDirs(m_dir);
	if (!WriteAllBytes(m_file, enc))
	{
		Log::Write(L"AccountStore: falha ao gravar accounts.dat");
		return false;
	}
	return true;
}

void AccountStore::Add(const Account& a) { m_accounts.push_back(a); }

void AccountStore::Set(size_t i, const Account& a)
{
	if (i < m_accounts.size()) m_accounts[i] = a;
}

void AccountStore::Remove(size_t i)
{
	if (i < m_accounts.size()) m_accounts.erase(m_accounts.begin() + i);
}

}
