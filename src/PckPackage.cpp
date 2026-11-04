#include "PckPackage.h"
#include "Common.h"
#include "BuildConfig.h"
#include "Log.h"
#include <zlib.h>
#include <io.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace el {
namespace pck {

static const uint64_t MAX_FILE_PACKAGE = 0x7fffff00ULL;
static const uint32_t AFPCK_VERSION = 0x00020003;
static const uint32_t PACKFLAG_ENCRYPT = 0x80000000;
static const uint32_t SAFE_TAG = 0x4DCA23EF;
static const char AFPCK_COPYRIGHT_TAG[] =
	"Angelica File Package, Perfect World Co. Ltd. 2002~2008. All Rights Reserved. ";

static const Algo kDefault = { kPckGuard0, kPckGuard1, kPckCheckMask, kPckMask };
static const Algo kAlgo181 = { 0x0D0E195A, 0x22B2B603, 0xFB1C1DD2, 0xC50B1A77 };
static const Algo kAlgo111 = { 0xab12908f, 0xb3231902, 0x18734563, 0x2a63810e };

const Algo* DetectAlgo(uint32_t guard0, uint32_t guard1)
{
	if (guard0 == kDefault.guard0 && guard1 == kDefault.guard1) return &kDefault;
	if (guard0 == kAlgo181.guard0 && guard1 == kAlgo181.guard1) return &kAlgo181;
	if (guard0 == kAlgo111.guard0 && guard1 == kAlgo111.guard1) return &kAlgo111;
	Log::Write(Format(L"PckPackage: guard bytes desconhecidos (0x%08X 0x%08X), usando perfil principal", guard0, guard1));
	return &kDefault;
}

static bool IEquals(const std::string& a, const std::string& b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); ++i)
		if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
	return true;
}

static void EncryptBuf(uint8_t* p, uint32_t len, bool encrypt)
{
	if (!encrypt) return;
	uint32_t mask = len + 0x739802ab;
	for (uint32_t i = 0; i + 3 < len; i += 4)
	{
		uint32_t d = (p[i] << 24) | (p[i + 1] << 16) | (p[i + 2] << 8) | p[i + 3];
		if (encrypt) { d ^= mask; d = (d << 16) | (d >> 16); }
		else { d = (d << 16) | (d >> 16); d ^= mask; }
		p[i] = (d >> 24) & 0xff;
		p[i + 1] = (d >> 16) & 0xff;
		p[i + 2] = (d >> 8) & 0xff;
		p[i + 3] = d & 0xff;
	}
}

CombinedFile::CombinedFile()
	: m_f1(nullptr), m_f2(nullptr), m_size1(0), m_size2(0), m_pos(0)
{
}

CombinedFile::~CombinedFile() { Close(); }

bool CombinedFile::Open(const std::wstring& path, const char* mode)
{
	Close();
	std::wstring wmode(mode, mode + strlen(mode));
	m_f1 = _wfopen(path.c_str(), wmode.c_str());
	if (!m_f1) return false;
	_fseeki64(m_f1, 0, SEEK_END);
	m_size1 = _ftelli64(m_f1);
	_fseeki64(m_f1, 0, SEEK_SET);
	m_pos = 0;

	m_path2 = path;
	size_t dot = m_path2.find_last_of(L'.');
	if (dot != std::wstring::npos) m_path2 = m_path2.substr(0, dot);
	m_path2 += L".pkx";
	return true;
}

bool CombinedFile::Phase2Open(uint64_t offset)
{
	if (offset < MAX_FILE_PACKAGE) return true;
	if (m_f2) return true;
	m_f2 = _wfopen(m_path2.c_str(), L"r+b");
	if (!m_f2) m_f2 = _wfopen(m_path2.c_str(), L"wb+");
	if (!m_f2) return false;
	_fseeki64(m_f2, 0, SEEK_END);
	m_size2 = _ftelli64(m_f2);
	_fseeki64(m_f2, 0, SEEK_SET);
	return true;
}

void CombinedFile::Close()
{
	if (m_f2) { fclose(m_f2); m_f2 = nullptr; }
	if (m_f1) { fclose(m_f1); m_f1 = nullptr; }
	m_size1 = m_size2 = m_pos = 0;
}

size_t CombinedFile::Read(void* buffer, size_t size)
{
	uint64_t newPos = m_pos + size;
	uint8_t* out = (uint8_t*)buffer;
	if (newPos <= MAX_FILE_PACKAGE)
	{
		size_t r = fread(out, 1, size, m_f1);
		m_pos += r;
		return r;
	}
	if (m_pos < MAX_FILE_PACKAGE)
	{
		size_t r1 = (size_t)(MAX_FILE_PACKAGE - m_pos);
		size_t got1 = fread(out, 1, r1, m_f1);
		if (got1 != r1) { m_pos += got1; return got1; }
		size_t got2 = 0;
		if (m_f2) { _fseeki64(m_f2, 0, SEEK_SET); got2 = fread(out + r1, 1, size - r1, m_f2); }
		m_pos += got1 + got2;
		return got1 + got2;
	}
	size_t r = m_f2 ? fread(out, 1, size, m_f2) : 0;
	m_pos += r;
	return r;
}

size_t CombinedFile::Write(const void* buffer, size_t size)
{
	const uint8_t* in = (const uint8_t*)buffer;
	uint64_t newPos = m_pos + size;
	if (newPos <= MAX_FILE_PACKAGE)
	{
		size_t w = fwrite(in, 1, size, m_f1);
		m_pos += w;
		if (m_pos > m_size1) m_size1 = m_pos;
		return w;
	}
	if (m_pos < MAX_FILE_PACKAGE)
	{
		size_t s1 = (size_t)(MAX_FILE_PACKAGE - m_pos);
		size_t w1 = fwrite(in, 1, s1, m_f1);
		m_pos += w1;
		if (m_pos > m_size1) m_size1 = m_pos;
		if (w1 != s1) { _fseeki64(m_f1, m_pos, SEEK_SET); return w1; }
		m_size1 = MAX_FILE_PACKAGE;
		if (!m_f2) Phase2Open(MAX_FILE_PACKAGE);
		if (!m_f2) return w1;
		_fseeki64(m_f2, 0, SEEK_SET);
		size_t w2 = fwrite(in + s1, 1, size - s1, m_f2);
		m_pos += w2;
		if (m_pos > m_size1 + m_size2) m_size2 = m_pos - m_size1;
		return w1 + w2;
	}
	if (!m_f2) { Phase2Open(MAX_FILE_PACKAGE); if (m_f2) _fseeki64(m_f2, 0, SEEK_SET); }
	size_t w = m_f2 ? fwrite(in, 1, size, m_f2) : 0;
	m_pos += w;
	if (m_pos > m_size1 + m_size2) m_size2 = m_pos - m_size1;
	return w;
}

void CombinedFile::Seek(int64_t offset, int origin)
{
	if (m_f2)
	{
		int64_t newPos = (int64_t)m_pos;
		if (origin == SEEK_SET) newPos = offset;
		else if (origin == SEEK_CUR) newPos = (int64_t)m_pos + offset;
		else if (origin == SEEK_END) newPos = (int64_t)(m_size1 + m_size2) + offset;
		if (newPos < 0) newPos = 0;
		if ((uint64_t)newPos > m_size1 + m_size2) newPos = (int64_t)(m_size1 + m_size2);
		if ((uint64_t)newPos < m_size1) _fseeki64(m_f1, newPos, SEEK_SET);
		else _fseeki64(m_f2, newPos - m_size1, SEEK_SET);
		m_pos = (uint64_t)newPos;
	}
	else
	{
		_fseeki64(m_f1, offset, origin);
		m_pos = (uint64_t)_ftelli64(m_f1);
	}
}

void CombinedFile::Flush()
{
	if (m_f1) fflush(m_f1);
	if (m_f2) fflush(m_f2);
}

static bool TruncateFile(FILE* f, uint64_t size)
{
	if (!f) return false;
	fflush(f);
	intptr_t os = _get_osfhandle(_fileno(f));
	if (os == -1) return false;
	HANDLE h = (HANDLE)os;
	LARGE_INTEGER li; li.QuadPart = (LONGLONG)size;
	if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) return false;
	return SetEndOfFile(h) != FALSE;
}

void CombinedFile::Truncate(uint64_t size)
{
	if (m_f2)
	{
		if (size <= MAX_FILE_PACKAGE)
		{
			TruncateFile(m_f1, size);
			m_size1 = size;
			fclose(m_f2);
			m_f2 = nullptr;
			_wremove(m_path2.c_str());
			m_size2 = 0;
		}
		else
		{
			m_size2 = size - MAX_FILE_PACKAGE;
			TruncateFile(m_f2, m_size2);
		}
	}
	else
	{
		TruncateFile(m_f1, size);
		m_size1 = size;
	}
}

PckPackage::PckPackage()
	: m_open(false), m_readOnly(false), m_changed(false), m_hasSafeHeader(false), m_entryOffset(0), m_headerPadding(0)
{
	memset(&m_header, 0, sizeof(m_header));
	memset(&m_safeHeader, 0, sizeof(m_safeHeader));
}

PckPackage::~PckPackage()
{
	if (m_open) Close();
}

std::string PckPackage::NormalizeName(const std::string& in)
{
	std::string s = in;
	for (char& c : s) if (c == '/') c = '\\';
	std::string out;
	for (size_t i = 0; i < s.size(); ++i)
	{
		if (s[i] == '\\' && !out.empty() && out.back() == '\\') continue;
		out.push_back(s[i]);
	}
	if (out.size() > 2 && out[0] == '.' && out[1] == '\\') out = out.substr(2);
	while (!out.empty() && out.back() == ' ') out.pop_back();
	return out;
}

bool PckPackage::Open(const std::wstring& pckPath, bool readOnly)
{
	const char* mode = readOnly ? "rb" : "r+b";
	if (!m_file.Open(pckPath, mode))
	{
		if (!readOnly && m_file.Open(pckPath, "rb"))
			readOnly = true;
		else
			return false;
	}
	m_readOnly = readOnly;

	{
		size_t slash = pckPath.find_last_of(L"\\/");
		std::wstring file = (slash == std::wstring::npos) ? pckPath : pckPath.substr(slash + 1);
		size_t dot = file.find_last_of(L'.');
		if (dot != std::wstring::npos) file = file.substr(0, dot);
		m_folder = Narrow(file, 936) + "\\";
	}

	m_file.Seek(0, SEEK_SET);
	if (m_file.Read(&m_safeHeader, sizeof(SafeHeader)) == sizeof(SafeHeader) &&
		m_safeHeader.tag1 == SAFE_TAG)
	{
		m_hasSafeHeader = true;
		m_file.Phase2Open(m_safeHeader.offset);
	}
	else
		m_hasSafeHeader = false;

	m_file.Seek(0, SEEK_END);
	uint64_t nOffset = m_file.Tell();
	m_file.Seek(0, SEEK_SET);
	if (m_hasSafeHeader) nOffset = m_safeHeader.offset;

	uint32_t version = 0;
	m_file.Seek((int64_t)nOffset - (int64_t)sizeof(uint32_t), SEEK_SET);
	m_file.Read(&version, sizeof(uint32_t));
	if (version != AFPCK_VERSION)
	{
		Log::Write(L"PckPackage: versao de pacote nao suportada");
		m_file.Close();
		return false;
	}

	int32_t numFile = 0;
	m_file.Seek((int64_t)nOffset - (int64_t)(sizeof(int32_t) + sizeof(uint32_t)), SEEK_SET);
	m_file.Read(&numFile, sizeof(int32_t));

	// Localiza o FileHeader no fim do arquivo. O trailer pode ter 284 ou 288 bytes
	// (o CPWCreator/WinPCK v1.33 insere 4 bytes extras), entao procuramos pelos
	// guard bytes em vez de assumir um tamanho fixo.
	uint64_t headerPos = 0;
	{
		const uint32_t guards[3] = { kDefault.guard0, kAlgo181.guard0, kAlgo111.guard0 };
		const size_t WIN = 2048;
		size_t winSize = (size_t)((nOffset < (uint64_t)WIN) ? nOffset : (uint64_t)WIN);
		std::vector<uint8_t> win(winSize);
		m_file.Seek((int64_t)(nOffset - winSize), SEEK_SET);
		m_file.Read(win.data(), win.size());
		bool found = false;
		for (int i = (int)winSize - 4; i >= 0; --i)
		{
			uint32_t v = (uint32_t)win[i] | ((uint32_t)win[i + 1] << 8)
				| ((uint32_t)win[i + 2] << 16) | ((uint32_t)win[i + 3] << 24);
			for (uint32_t g : guards)
				if (v == g) { headerPos = (nOffset - winSize) + (uint64_t)i; found = true; break; }
			if (found) break;
		}
		if (!found)
			headerPos = nOffset - (uint64_t)(sizeof(FileHeader) + sizeof(uint32_t) + sizeof(int32_t));
	}
	m_file.Seek((int64_t)headerPos, SEEK_SET);
	m_file.Read(&m_header, sizeof(FileHeader));
	m_headerPadding = (int)((nOffset - headerPos) - (uint64_t)(sizeof(FileHeader) + 8));
	if (m_headerPadding < 0 || m_headerPadding > 64) m_headerPadding = 0;

	if (strstr(m_header.szDescription, "File Package") == nullptr)
	{
		Log::Write(L"PckPackage: descricao do pacote invalida");
		m_file.Close();
		return false;
	}

	const Algo* algo = DetectAlgo(m_header.guardByte0, m_header.guardByte1);
	if (!algo)
	{
		Log::Write(L"PckPackage: guard bytes desconhecidos");
		m_file.Close();
		return false;
	}
	m_algo = *algo;
	m_header.dwEntryOffset ^= (0xFFFFFFFF00000000ULL | (uint64_t)m_algo.mask);
	m_header.dwVersion = AFPCK_VERSION;

	m_file.Seek((int64_t)m_header.dwEntryOffset, SEEK_SET);

	m_entries.clear();
	m_entries.reserve(numFile > 0 ? numFile : 0);
	for (int i = 0; i < numFile; ++i)
	{
		int32_t compSize = 0, checkSize = 0;
		if (m_file.Read(&compSize, sizeof(int32_t)) != sizeof(int32_t)) { m_file.Close(); return false; }
		if (m_file.Read(&checkSize, sizeof(int32_t)) != sizeof(int32_t)) { m_file.Close(); return false; }
		compSize ^= (int32_t)m_algo.mask;
		checkSize = checkSize ^ (int32_t)m_algo.checkMask ^ (int32_t)m_algo.mask;
		if (compSize != checkSize || compSize <= 0)
		{
			Log::Write(L"PckPackage: check byte invalido");
			m_file.Close();
			return false;
		}

		std::vector<uint8_t> rec(compSize);
		if (m_file.Read(rec.data(), compSize) != (size_t)compSize) { m_file.Close(); return false; }

		FileEntry entry;
		if (compSize == (int)sizeof(FileEntry))
		{
			memcpy(&entry, rec.data(), sizeof(FileEntry));
		}
		else
		{
			uLongf outLen = sizeof(FileEntry);
			if (uncompress((Bytef*)&entry, &outLen, rec.data(), compSize) != Z_OK || outLen != sizeof(FileEntry))
			{
				Log::Write(L"PckPackage: falha ao descomprimir entrada");
				m_file.Close();
				return false;
			}
		}
		entry.szFileName[sizeof(entry.szFileName) - 1] = '\0';
		m_entries.push_back(entry);
	}

	m_entryOffset = m_header.dwEntryOffset;
	m_changed = false;
	m_open = true;
	return true;
}

bool PckPackage::Create(const std::wstring& pckPath)
{
	if (!m_file.Open(pckPath, "wb")) return false;

	size_t slash = pckPath.find_last_of(L"\\/");
	std::wstring file = (slash == std::wstring::npos) ? pckPath : pckPath.substr(slash + 1);
	size_t dot = file.find_last_of(L'.');
	if (dot != std::wstring::npos) file = file.substr(0, dot);
	m_folder = Narrow(file, 936) + "\\";

	m_hasSafeHeader = true;
	m_safeHeader.tag1 = SAFE_TAG;
	m_safeHeader.offset = 0;

	memset(&m_header, 0, sizeof(m_header));
	m_header.guardByte0 = kDefault.guard0;
	m_header.guardByte1 = kDefault.guard1;
	m_header.dwVersion = AFPCK_VERSION;
	m_header.dwFlags = 0;
	m_header.dwEntryOffset = sizeof(SafeHeader);
	strncpy(m_header.szDescription, AFPCK_COPYRIGHT_TAG, sizeof(m_header.szDescription));
	m_algo = kDefault;

	m_entries.clear();
	m_entryOffset = sizeof(SafeHeader);
	m_readOnly = false;
	m_changed = false;
	m_open = true;
	return true;
}

bool PckPackage::GetEntry(const std::string& name, FileEntry& out)
{
	std::string norm = NormalizeName(name);
	for (const FileEntry& e : m_entries)
	{
		std::string en(e.szFileName);
		if (IEquals(NormalizeName(en), norm)) { out = e; return true; }
	}
	return false;
}

bool PckPackage::GetPacked(const std::string& name, std::vector<uint8_t>& packedOut)
{
	FileEntry e;
	if (!GetEntry(name, e)) return false;
	if (e.dwCompressedLength > MAX_FILE_PACKAGE) return false;

	std::vector<uint8_t> data(e.dwCompressedLength);
	m_file.Seek((int64_t)e.dwOffset, SEEK_SET);
	if (m_file.Read(data.data(), data.size()) != data.size()) return false;
	if (m_header.dwFlags & PACKFLAG_ENCRYPT) EncryptBuf(data.data(), (uint32_t)data.size(), false);

	packedOut.resize(4 + data.size());
	packedOut[0] = e.dwLength & 0xFF;
	packedOut[1] = (e.dwLength >> 8) & 0xFF;
	packedOut[2] = (e.dwLength >> 16) & 0xFF;
	packedOut[3] = (e.dwLength >> 24) & 0xFF;
	memcpy(packedOut.data() + 4, data.data(), data.size());
	return true;
}

bool PckPackage::AddOrReplacePacked(const std::string& name, const uint8_t* packed, size_t packedLen)
{
	if (m_readOnly || !m_open || packedLen < 4) return false;

	uint32_t origLen = (uint32_t)packed[0] | ((uint32_t)packed[1] << 8)
		| ((uint32_t)packed[2] << 16) | ((uint32_t)packed[3] << 24);
	const uint8_t* data = packed + 4;
	uint32_t compLen = (uint32_t)(packedLen - 4);

	std::string norm = NormalizeName(name);
	FileEntry* target = nullptr;
	for (FileEntry& e : m_entries)
	{
		std::string en(e.szFileName);
		if (IEquals(NormalizeName(en), norm)) { target = &e; break; }
	}
	if (!target)
	{
		FileEntry e;
		memset(&e, 0, sizeof(e));
		strncpy(e.szFileName, norm.c_str(), sizeof(e.szFileName) - 1);
		e.unknown = 0; e.unknown2 = 0; e.iAccessCnt = 0;
		m_entries.push_back(e);
		target = &m_entries.back();
	}
	target->dwOffset = m_entryOffset;
	target->dwLength = origLen;
	target->dwCompressedLength = compLen;

	std::vector<uint8_t> tmp(data, data + compLen);
	if (m_header.dwFlags & PACKFLAG_ENCRYPT) EncryptBuf(tmp.data(), compLen, true);

	m_file.Seek((int64_t)m_entryOffset, SEEK_SET);
	if (m_file.Write(tmp.data(), tmp.size()) != tmp.size()) return false;
	m_entryOffset += compLen;
	m_file.Flush();
	m_changed = true;
	return true;
}

bool PckPackage::Remove(const std::string& name)
{
	if (m_readOnly) return false;
	std::string norm = NormalizeName(name);
	for (size_t i = 0; i < m_entries.size(); ++i)
	{
		std::string en(m_entries[i].szFileName);
		if (IEquals(NormalizeName(en), norm))
		{
			m_entries.erase(m_entries.begin() + i);
			m_changed = true;
			return true;
		}
	}
	return false;
}

bool PckPackage::Save()
{
	m_header.dwEntryOffset = m_entryOffset;
	m_header.guardByte0 = m_algo.guard0;
	m_header.guardByte1 = m_algo.guard1;
	m_header.dwVersion = AFPCK_VERSION;
	strncpy(m_header.szDescription, AFPCK_COPYRIGHT_TAG, sizeof(m_header.szDescription));

	uint64_t fileSize = m_entryOffset;
	m_file.Seek((int64_t)m_entryOffset, SEEK_SET);

	uint64_t entryBytes = 0;
	for (const FileEntry& e : m_entries)
	{
		uLongf compLen = compressBound(sizeof(FileEntry));
		std::vector<uint8_t> rec(compLen);
		int ret = compress2(rec.data(), &compLen, (const Bytef*)&e, sizeof(FileEntry), 1);
		if (ret != Z_OK || compLen >= sizeof(FileEntry))
		{
			compLen = sizeof(FileEntry);
			rec.assign((const uint8_t*)&e, (const uint8_t*)&e + sizeof(FileEntry));
		}
		rec.resize(compLen);

		uint32_t a = (uint32_t)compLen ^ m_algo.mask;
		uint32_t b = (uint32_t)compLen ^ m_algo.mask ^ m_algo.checkMask;
		m_file.Write(&a, sizeof(uint32_t));
		m_file.Write(&b, sizeof(uint32_t));
		m_file.Write(rec.data(), rec.size());
		entryBytes += sizeof(uint32_t) + sizeof(uint32_t) + rec.size();
	}
	fileSize += entryBytes;

	uint64_t offMask = 0xFFFFFFFF00000000ULL | (uint64_t)m_algo.mask;
	m_header.dwEntryOffset ^= offMask;
	m_file.Write(&m_header, sizeof(FileHeader));
	m_header.dwEntryOffset ^= offMask;
	fileSize += sizeof(FileHeader);

	if (m_headerPadding > 0)
	{
		std::vector<uint8_t> pad(m_headerPadding, 0);
		m_file.Write(pad.data(), pad.size());
		fileSize += m_headerPadding;
	}

	int32_t numFile = (int32_t)m_entries.size();
	m_file.Write(&numFile, sizeof(int32_t));
	fileSize += sizeof(int32_t);

	uint32_t version = AFPCK_VERSION;
	m_file.Write(&version, sizeof(uint32_t));
	fileSize += sizeof(uint32_t);

	m_file.Truncate(fileSize);

	if (m_hasSafeHeader)
	{
		m_file.Seek(0, SEEK_END);
		m_safeHeader.offset = m_file.Tell();
		m_file.Seek(0, SEEK_SET);
		m_file.Write(&m_safeHeader, sizeof(SafeHeader));
	}
	m_file.Flush();
	return true;
}

bool PckPackage::Close()
{
	bool ok = true;
	if (m_open && m_changed && !m_readOnly)
		ok = Save();
	m_file.Close();
	m_open = false;
	m_entries.clear();
	return ok;
}

static const wchar_t* kPackRoots[] = {
	L"configs", L"facedata", L"gfx", L"grasses", L"help\\script", L"interfaces",
	L"building", L"models", L"sfx", L"shaders", L"surfaces", L"textures",
	L"trees", L"litmodels", L"loddata", L"script"
};

PckManager::PckManager() : m_usePack(true) {}
PckManager::~PckManager() { FlushAll(); for (auto& kv : m_open) delete kv.second; m_open.clear(); }

void PckManager::Init(const std::wstring& elementDir, bool usePack)
{
	m_elementDir = elementDir;
	m_usePack = usePack;
	for (auto* r : kPackRoots) m_roots.push_back(Narrow(r, 936));
}

static std::wstring NormalizeW(const std::wstring& in)
{
	std::wstring s = in;
	for (wchar_t& c : s) if (c == L'/') c = L'\\';
	while (s.size() > 2 && s[0] == L'.' && s[1] == L'\\') s = s.substr(2);
	return ToLower(s);
}

bool PckManager::IsInPack(const std::wstring& decodedPath)
{
	if (!m_usePack) return false;
	std::wstring p = NormalizeW(decodedPath);
	for (const std::string& root : m_roots)
	{
		std::wstring r = Widen(root, 936);
		if (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == L'\\')
			return true;
	}
	return false;
}

PckPackage* PckManager::Resolve(const std::wstring& decodedPath)
{
	if (!m_usePack) return nullptr;
	std::wstring p = NormalizeW(decodedPath);

	std::wstring best;
	for (const std::string& root : m_roots)
	{
		std::wstring r = Widen(root, 936);
		if (p.size() > r.size() && p.compare(0, r.size(), r) == 0 && p[r.size()] == L'\\')
			if (r.size() > best.size()) best = r;
	}
	if (best.empty()) return nullptr;

	std::wstring pckPath = m_elementDir;
	if (!pckPath.empty() && pckPath.back() != L'\\') pckPath += L'\\';
	pckPath += best + L".pck";

	auto it = m_open.find(pckPath);
	if (it != m_open.end()) return it->second;

	PckPackage* pkg = new PckPackage();
	if (!pkg->Open(pckPath, false))
	{
		delete pkg;
		return nullptr;
	}
	m_open[pckPath] = pkg;
	return pkg;
}

bool PckManager::FlushAll()
{
	bool ok = true;
	for (auto& kv : m_open)
		if (!kv.second->Close()) ok = false;
	return ok;
}

}
}
