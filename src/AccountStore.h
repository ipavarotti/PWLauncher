#pragma once
#include <string>
#include <vector>

namespace el {

// Uma conta salva do jogador. A senha e criptografada (DPAPI) tanto no campo
// quanto no arquivo inteiro (ver AccountStore.cpp).
struct Account
{
	std::wstring login;
	std::wstring password;
	std::wstring role;   // personagem opcional (para "role:" do elementclient)
};

// Persistencia das contas dentro da pasta JA existente do cliente:
// <elementDir>\userdata\accounts.dat  (blob criptografado com DPAPI).
// Tambem importa os nomes que o proprio jogo salva em
// <elementDir>\userdata\accounts.txt (Base64 UTF-16LE).
class AccountStore
{
public:
	AccountStore();

	bool Load(const std::wstring& elementDir);
	bool Save() const;

	const std::vector<Account>& All() const { return m_accounts; }
	size_t Count() const { return m_accounts.size(); }
	const Account& Get(size_t i) const { return m_accounts[i]; }

	void Add(const Account& a);
	void Set(size_t i, const Account& a);
	void Remove(size_t i);

private:
	std::wstring m_dir;
	std::wstring m_file;
	std::vector<Account> m_accounts;
};

}
