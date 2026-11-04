#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace el {

std::vector<uint8_t> PackBuffer(const uint8_t* data, size_t len);
bool UnpackBuffer(const uint8_t* data, size_t len, std::vector<uint8_t>& rawOut);

bool PackFile(const std::wstring& src, std::vector<uint8_t>& packedOut);
bool UnpackFileTo(const std::wstring& packedSrc, const std::wstring& dest);

bool PackedMd5OfFile(const std::wstring& path, std::string& hexOut);
std::string PackedMd5OfBuffer(const uint8_t* data, size_t len);

// Nivel de compressao zlib usado para recomprimir o arquivo local na verificacao.
// Precisa bater com o usado pelo CPWCreator (padrao: 9).
void SetCompressionLevel(int level);
int  CompressionLevel();

}
