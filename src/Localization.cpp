#include "Localization.h"
#include "Common.h"
#include "Json.h"

namespace el {

std::map<std::wstring, std::wstring> Localization::s_strings;
std::wstring Localization::s_current = L"en";
std::vector<std::wstring> Localization::s_searchDirs;

// Fallback embutido (UTF-8) para nunca exibir chaves cruas se Locales/*.json
// nao estiverem ao lado do executavel. Nao sobrepoe valores vindos de arquivo.
static const struct { const char* k; const char* v; } kFallback[] = {
	{ "ui.play", "Jogar" },
	{ "ui.check_updates", "Verificar atualiza\u00e7\u00f5es" },
	{ "ui.website", "Site" },
	{ "ui.profile", "Painel" },
	{ "ui.discord", "Discord" },
	{ "ui.updating_files", "Atualizando arquivos" },
	{ "ui.ready_to_play", "Pronto para jogar" },
	{ "ui.checking_updates", "Verificando atualiza\u00e7\u00f5es" },
	{ "ui.downloading", "Baixando" },
	{ "ui.installing", "Instalando" },
	{ "ui.error", "Erro" },
	{ "ui.completed", "Conclu\u00eddo" },
	{ "ui.language", "Idioma" },
	{ "ui.settings", "Configura\u00e7\u00f5es" },
	{ "ui.updating", "Atualizando" },
	{ "ui.restarting_launcher", "Reiniciando o launcher..." },
	{ "ui.status_check_lists", "Buscando lista de atualiza\u00e7\u00f5es..." },
	{ "ui.status_processing_file", "Processando arquivo {0}/{1}..." },
	{ "ui.status_applying_data", "Instalando dados do jogo..." },
	{ "ui.status_applying_update_pck", "Aplicando cont\u00eainer de atualiza\u00e7\u00e3o..." },
	{ "ui.status_installing_update", "Instalando atualiza\u00e7\u00e3o..." },
	{ "ui.status_check_self_update", "Verificando autoatualiza\u00e7\u00e3o do launcher..." },
	{ "ui.status_downloading_dll", "Baixando pckdll.dll..." },
	{ "ui.status_self_update_scheduled", "Autoatualiza\u00e7\u00e3o agendada. Reiniciando..." },
	{ "ui.drivers_missing", "Pasta de drivers n\u00e3o encontrada (DirectX/VC++)." },
	{ "ui.eta_only", "Restante aproximadamente: {0}" },
	{ "ui.stage_downloading_update", "Baixando atualiza\u00e7\u00e3o {0}" },
	{ "ui.stage_installing_update", "Instalando atualiza\u00e7\u00e3o {0}" },
	{ "ui.updates_left", "Atualiza\u00e7\u00f5es restantes: {0}" },
	{ "ui.game_not_found", "Execut\u00e1vel do jogo n\u00e3o encontrado" },
	{ "ui.game_start_failed", "Falha ao iniciar o jogo" },
	{ "ui.guild_icons_check", "Verificando \u00edcones de guilda..." },
	{ "ui.guild_icons_updating", "Atualizando \u00edcones de guilda..." },
	{ "ui.close_game_before_update", "O cliente do jogo est\u00e1 em execu\u00e7\u00e3o. Feche-o antes de instalar atualiza\u00e7\u00f5es." },
	{ "ui.pck_error_title", "Erro ao atualizar PCK" },
	{ "ui.update_available_confirm", "Uma atualiza\u00e7\u00e3o do jogo est\u00e1 dispon\u00edvel. Instalar agora?" },
	{ "ui.yes", "Sim" },
	{ "ui.no", "N\u00e3o" },
	{ "ui.read_more", "Saiba mais" },
	{ "ui.game_start_error_access_denied", "Acesso negado. Tente executar o launcher como administrador." },
	{ "ui.game_start_error_file_not_found", "Arquivo execut\u00e1vel do jogo n\u00e3o encontrado ou foi deletado." },
	{ "ui.insufficient_disk_space", "Espa\u00e7o em disco insuficiente. Pelo menos 3 GB de espa\u00e7o livre s\u00e3o necess\u00e1rios para atualizar o jogo." },
	{ "ui.disk_space_check_failed", "Falha ao verificar o espa\u00e7o dispon\u00edvel em disco." },
	{ "ui.update_check_timeout", "Tempo de conex\u00e3o ao servidor de atualiza\u00e7\u00e3o esgotado. Tente novamente mais tarde. Se o problema persistir, entre em contato com a administra\u00e7\u00e3o do servidor." },
	{ "ui.update_server_unreachable", "Servidor de atualiza\u00e7\u00e3o inacess\u00edvel. Verifique sua conex\u00e3o com a internet e tente novamente. Se o problema persistir, entre em contato com a administra\u00e7\u00e3o do servidor." },
	{ "ui.update_network_error", "Erro de rede ao verificar atualiza\u00e7\u00f5es. Tente novamente mais tarde. Se o problema persistir, entre em contato com a administra\u00e7\u00e3o do servidor." },
	{ "ui.update_download_failed", "O download da atualiza\u00e7\u00e3o foi interrompido. Verifique sua conex\u00e3o com a internet e tente novamente. Se o problema persistir, entre em contato com a administra\u00e7\u00e3o do servidor." },
	{ "ui.update_installation_failed", "Falha ao instalar a atualiza\u00e7\u00e3o. Tente novamente. Se o problema persistir, entre em contato com a administra\u00e7\u00e3o do servidor." },
	{ "ui.verifying", "Verificando arquivos..." },
	{ "ui.verify_done", "Verifica\u00e7\u00e3o conclu\u00edda: {0} ok, {1} reparado(s), {2} ausente(s)" },
	{ "ui.verify_files", "Verificar arquivos" },
	{ "ui.accounts", "Contas" },
	{ "ui.account_new", "Nova conta" },
	{ "ui.account_edit", "Editar conta" },
	{ "ui.account_login", "Login" },
	{ "ui.account_password", "Senha" },
	{ "ui.account_character", "Personagem (opcional)" },
	{ "ui.account_hint_empty", "Nenhuma conta salva." },
	{ "ui.account_hint_pw", "Clique em + para adicionar uma conta e entrar direto." },
	{ "ui.click_to_login", "Clique para entrar" },
	{ "ui.account_delete_confirm", "Remover a conta {0}?" },
	{ "ui.login", "Entrar" },
	{ "ui.save", "Salvar" },
	{ "ui.cancel", "Cancelar" },
};

void Localization::Initialize(const std::wstring& lang)
{
	s_searchDirs.clear();
	std::wstring exeDir = GetModuleDir();
	s_searchDirs.push_back(JoinPath(exeDir, L"Locales"));
	s_searchDirs.push_back(JoinPath(JoinPath(exeDir, L".."), L"Locales"));
	s_current = lang.empty() ? L"en" : lang;
	Load(s_current);
}

void Localization::SetLanguage(const std::wstring& lang)
{
	if (lang.empty() || ToLower(lang) == ToLower(s_current)) return;
	s_current = lang;
	Load(s_current);
}

const std::wstring& Localization::CurrentLanguage()
{
	return s_current;
}

void Localization::Load(const std::wstring& lang)
{
	s_strings.clear();

	std::string text;
	std::wstring found;
	for (const std::wstring& dir : s_searchDirs)
	{
		std::wstring path = JoinPath(dir, lang + L".json");
		if (ReadFileUtf8(path, text) && !text.empty()) { found = path; break; }
	}

	if (!found.empty())
	{
		json::Value root; std::string jerr;
		if (json::Parse(text, root, jerr) && root.IsObject())
		{
			for (const auto& kv : root.obj)
			{
				if (kv.second.IsString())
					s_strings[Widen(kv.first, CP_UTF8)] = Widen(kv.second.str, CP_UTF8);
			}
		}
	}

	// Preenche o que faltar com o fallback embutido (PT).
	for (const auto& e : kFallback)
	{
		std::wstring key = Widen(e.k, CP_UTF8);
		if (s_strings.find(key) == s_strings.end())
			s_strings[key] = Widen(e.v, CP_UTF8);
	}
}

std::wstring Localization::Get(const std::wstring& key)
{
	auto it = s_strings.find(key);
	if (it != s_strings.end()) return it->second;
	return key;
}

std::wstring Localization::Get(const char* key)
{
	if (!key) return std::wstring();
	return Get(Widen(key, CP_UTF8));
}

std::wstring Localization::Format(const std::wstring& pattern, const std::vector<std::wstring>& args)
{
	std::wstring out;
	out.reserve(pattern.size() + 32);
	for (size_t i = 0; i < pattern.size(); ++i)
	{
		if (pattern[i] == L'{' && i + 2 < pattern.size())
		{
			size_t close = pattern.find(L'}', i + 1);
			if (close != std::wstring::npos)
			{
				std::wstring idx = pattern.substr(i + 1, close - i - 1);
				bool allDigits = !idx.empty();
				for (wchar_t c : idx) if (c < L'0' || c > L'9') { allDigits = false; break; }
				if (allDigits)
				{
					size_t n = (size_t)_wtoi(idx.c_str());
					if (n < args.size()) out += args[n];
					i = close;
					continue;
				}
			}
		}
		out.push_back(pattern[i]);
	}
	return out;
}

}
