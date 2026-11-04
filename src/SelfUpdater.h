#pragma once
#include "Config.h"
#include <string>
#include <functional>

namespace el {

// Self-update do proprio launcher, adaptado ao servidor classico:
//   <base>launcher/version          -> versao (texto) da launcher no servidor
//   <base>launcher/<exeName>        -> binario novo (opcional .md5 ao lado)
// Se os recursos nao existirem (404), a checagem falha em silencio.
//
// O modo --replace e identico ao do launcher de referencia: substitui o exe em
// disco com o arquivo .new e reinicia.
class SelfUpdater
{
public:
	using LogFn = std::function<void(const std::wstring&)>;

	explicit SelfUpdater(AppConfig& cfg, LogFn log);

	// Trata os argumentos de linha de comando do modo replace.
	// Retorna true se o processo deve encerrar (replace ja executado).
	static bool HandleReplaceMode(int argc, wchar_t** argv);

	// Detecta se ha atualizacao do launcher no servidor.
	bool NeedsSelfUpdate(std::wstring& remoteVersion);

	// Baixa o novo launcher e dispara o modo replace. Retorna true se agendou.
	bool PerformSelfUpdate(const std::wstring& remoteVersion);

private:
	AppConfig& m_cfg;
	LogFn m_log;
};

}
