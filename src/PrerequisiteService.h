#pragma once
#include <string>
#include <functional>

namespace el {

// Verifica/instala pre-requisitos (VC++ 2015-2022 Redistributable e DirectX 9),
// no mesmo espirito do launcher de referencia. Os instaladores sao procurados em
// <installDir>\drivers.
class PrerequisiteService
{
public:
	PrerequisiteService(const std::wstring& installDir, std::function<void(const std::wstring&)> log);

	// Retorna true se tudo ja estava instalado ou foi instalado com sucesso.
	bool EnsureInstalled();

	bool IsVcRedistInstalled();
	bool IsDirectX9Installed();

private:
	bool TryInstallVcRedist();
	bool TryInstallDirectX9();
	bool RunInstaller(const std::wstring& exe, const std::wstring& args);

	std::wstring m_driversDir;
	std::wstring m_baseDir;
	std::function<void(const std::wstring&)> m_log;
};

}
