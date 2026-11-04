#pragma once
#include <string>
#include <vector>
#include <map>

namespace el {

// Localizacao no estilo do launcher de referencia: strings carregadas de
// Locales/<lang>.json (UTF-8). A busca procura em <exeDir>\Locales e, em seguida,
// no diretorio pai (layout classico com o exe em launcher\).
class Localization
{
public:
	static void Initialize(const std::wstring& lang);
	static void SetLanguage(const std::wstring& lang);
	static const std::wstring& CurrentLanguage();
	static std::wstring Get(const std::wstring& key);
	static std::wstring Get(const char* key);

	// Substitui apenas {0}, {1}, ... pelos argumentos fornecidos.
	static std::wstring Format(const std::wstring& pattern, const std::vector<std::wstring>& args);

private:
	static void Load(const std::wstring& lang);

	static std::map<std::wstring, std::wstring> s_strings;
	static std::wstring s_current;
	static std::vector<std::wstring> s_searchDirs;
};

}
