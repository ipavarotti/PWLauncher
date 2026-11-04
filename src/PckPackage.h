#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace el {

namespace pck {

#pragma pack(push, 1)
struct FileEntry
{
	char     szFileName[260];
	int32_t  unknown;
	uint32_t dwOffset;
	uint32_t dwLength;
	uint32_t dwCompressedLength;
	int32_t  iAccessCnt;
	int32_t  unknown2;
};

struct FileHeader
{
	uint32_t guardByte0;
	uint32_t dwVersion;
	uint64_t dwEntryOffset;
	uint32_t dwFlags;
	char     szDescription[252];
	uint32_t guardByte1;
};

struct SafeHeader
{
	uint32_t tag1;
	uint64_t offset;
};
#pragma pack(pop)

struct Algo
{
	uint32_t guard0;
	uint32_t guard1;
	uint32_t checkMask;
	uint32_t mask;
};

const Algo* DetectAlgo(uint32_t guard0, uint32_t guard1);

class CombinedFile
{
public:
	CombinedFile();
	~CombinedFile();

	bool Open(const std::wstring& path, const char* mode);
	bool Phase2Open(uint64_t offset);
	void Close();

	size_t Read(void* buffer, size_t size);
	size_t Write(const void* buffer, size_t size);
	void Seek(int64_t offset, int origin);
	uint64_t Tell() const { return m_pos; }
	void Flush();
	void Truncate(uint64_t size);

private:
	FILE* m_f1;
	FILE* m_f2;
	std::wstring m_path2;
	uint64_t m_size1;
	uint64_t m_size2;
	uint64_t m_pos;
};

class PckPackage
{
public:
	PckPackage();
	~PckPackage();

	bool Open(const std::wstring& pckPath, bool readOnly);
	bool Create(const std::wstring& pckPath);
	bool Close();

	bool IsOpen() const { return m_open; }
	bool IsReadOnly() const { return m_readOnly; }
	const std::string& Folder() const { return m_folder; }
	int EntryCount() const { return (int)m_entries.size(); }

	bool GetEntry(const std::string& name, FileEntry& out);
	const FileEntry* EntryAt(int index) const
	{
		if (index < 0 || index >= (int)m_entries.size()) return nullptr;
		return &m_entries[index];
	}
	bool GetPacked(const std::string& name, std::vector<uint8_t>& packedOut);
	bool AddOrReplacePacked(const std::string& name, const uint8_t* packed, size_t packedLen);
	bool Remove(const std::string& name);

private:
	bool Save();

	static std::string NormalizeName(const std::string& in);

	CombinedFile m_file;
	bool m_open;
	bool m_readOnly;
	bool m_changed;
	bool m_hasSafeHeader;
	Algo m_algo;
	FileHeader m_header;
	SafeHeader m_safeHeader;
	uint64_t m_entryOffset;
	int m_headerPadding;
	std::string m_folder;
	std::vector<FileEntry> m_entries;
};

class PckManager
{
public:
	PckManager();
	~PckManager();

	void Init(const std::wstring& elementDir, bool usePack);
	bool IsInPack(const std::wstring& decodedPath);
	PckPackage* Resolve(const std::wstring& decodedPath);
	bool FlushAll();

private:
	std::wstring m_elementDir;
	bool m_usePack;
	std::map<std::wstring, PckPackage*> m_open;
	std::vector<std::string> m_roots;
};

}

}
