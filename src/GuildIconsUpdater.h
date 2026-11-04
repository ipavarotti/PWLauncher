#pragma once
#include "Config.h"
#include <string>
#include <functional>

namespace el {

// Atualiza os icones de guilda (iconlist_guild.dds/.txt) dentro de surfaces.pck.
// Adaptado do launcher de referencia, mas controlado pelas URLs do config.json
// (ClanIcons.VersionUrl/PngUrl/TxtUrl). Se as URLs estiverem vazias, nao faz nada.
//
// Fluxo (servidor classico / HTTP simples):
//   1. GET VersionUrl  -> versao remota (texto). Igual a local => nada a fazer.
//   2. GET PngUrl      -> PNG convertido para DDS (BC1/DXT5) em memoria.
//   3. GET TxtUrl      -> salvo como esta.
//   4. Grava surfaces\iconset\iconlist_guild.{dds,txt} dentro do surfaces.pck.
//   5. Salva ClanIcons.Version no config.json.
class GuildIconsUpdater
{
public:
	using LogFn = std::function<void(const std::wstring&)>;

	GuildIconsUpdater(AppConfig& cfg, LogFn log);

	// Retorna true se aplicou uma atualizacao.
	bool Run();

private:
	std::wstring CacheBust(const std::wstring& url) const;

	AppConfig& m_cfg;
	LogFn m_log;
};

}
