#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include "UpdateEngine.h"
#include "Config.h"
#include "NewsService.h"
#include "AccountStore.h"

namespace el {

struct RectI { int x = 0, y = 0, w = 0, h = 0; };

// UI no estilo do launcher de referencia (.NET/WPF): 800x500, fundo img/bg.png,
// barra de titulo com logo + links + idioma + minimizar/fechar, cards de noticias,
// barra inferior com status/velocidade/percentual/progresso/versao e botao JOGAR.
class LauncherWindow
{
public:
	LauncherWindow(AppConfig& cfg, UpdateEngine& engine);
	~LauncherWindow();

	bool Create(HINSTANCE hInst);
	HWND Handle() const { return m_hwnd; }

	void SetOnPlay(std::function<void()> cb) { m_onPlay = cb; }
	void SetOnClose(std::function<void()> cb) { m_onClose = cb; }
	void SetOnVerify(std::function<void()> cb) { m_onVerify = cb; }
	void SetOnCheckUpdates(std::function<void()> cb) { m_onCheckUpdates = cb; }
	void SetOnLoginAccount(std::function<void(int)> cb) { m_onLoginAccount = cb; }

	AccountStore& Accounts() { return m_accounts; }

	void Refresh();
	void ShowError(const std::wstring& msg);
	void SetNews(std::vector<NewsItem> items);

private:
	// ----- contas (janela separada) -----
	void LoadAccounts();
	void SaveAccounts();
	void LayoutAccounts();
	void ComputeAccountRects();
	void DrawAccounts(Gdiplus::Graphics& g);
	void DrawAccountForm(Gdiplus::Graphics& g);
	void OpenAccountForm(int index);
	void CloseAccountForm();
	void CommitAccountForm();
	void FocusFormField(int index);

	bool CreateAccountsWindow();
	void ShowAccountsWindow();
	void RenderAccounts();
	static LRESULT CALLBACK AccountsWndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT OnAccountsMessage(HWND, UINT, WPARAM, LPARAM);

	static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
	LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

	void LoadLayout();
	void LoadImages();
	Gdiplus::Image* LoadImage(const std::wstring& rel);
	void Render();

	int  Measure(const std::wstring& text, int size, bool bold);
	void DrawTextU(Gdiplus::Graphics& g, const std::wstring& text, int x, int y, int w, int h,
		int size, bool bold, DWORD argb, int align = 0, bool wrap = false);
	void FillRound(Gdiplus::Graphics& g, const RectI& r, int radius, DWORD argb);
	void StrokeRound(Gdiplus::Graphics& g, const RectI& r, int radius, DWORD argb, float w);
	void DrawHLine(Gdiplus::Graphics& g, int x, int y, int w, DWORD argb);
	void DrawNavButton(Gdiplus::Graphics& g, int id, const std::wstring& text);
	void DrawPlayButton(Gdiplus::Graphics& g, bool enabled);
	void DrawAccountsButton(Gdiplus::Graphics& g);
	void DrawProgress(Gdiplus::Graphics& g, const RectI& r, int percent);
	void DrawNews(Gdiplus::Graphics& g);
	void DrawLangCombo(Gdiplus::Graphics& g);
	void DrawLangDropdown(Gdiplus::Graphics& g);

	bool Hit(const RectI& r, POINT p) const { return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h; }
	int  HitButton(POINT p) const;

	AppConfig& m_cfg;
	UpdateEngine& m_engine;
	HWND m_hwnd = nullptr;
	HINSTANCE m_hInst = nullptr;
	std::function<void()> m_onPlay, m_onClose, m_onVerify, m_onCheckUpdates;
	std::function<void(int)> m_onLoginAccount;

	// ----- contas (janela separada) -----
	AccountStore m_accounts;
	HWND m_acctHwnd = nullptr;
	int m_acctWinW = 470, m_acctWinH = 500;
	RectI m_acctClose, m_acctMin, m_acctPill, m_acctDrag;
	RectI m_acctPanel, m_acctAdd, m_acctSave, m_acctCancel;
	std::vector<RectI> m_acctRows;      // area clicavel (login) por conta
	std::vector<RectI> m_acctRemove;    // botao remover por conta
	std::vector<RectI> m_acctEdit;      // botao editar por conta
	std::vector<RectI> m_formFields;    // login/senha/personagem
	int m_acctHover = -1;               // -1 nenhum, -2 botao adicionar, >=0 linha
	int m_acctItemHover = -1;           // 0 remover, 1 editar, 2 adicionar, campos >=10
	bool m_acctFormOpen = false;
	int m_acctEditIndex = -1;           // -1 = nova conta
	int m_formFocus = 0;                // 0 login, 1 senha, 2 personagem
	std::wstring m_fLogin, m_fPassword, m_fRole;

	Gdiplus::Image* m_bg = nullptr;
	Gdiplus::Image* m_logo = nullptr;
	int m_width = 800, m_height = 500;

	RectI m_drag;
	RectI m_play, m_min, m_close, m_langCombo;
	RectI m_btnWebsite, m_btnProfile, m_btnDiscord, m_btnCheck, m_btnVerify, m_btnAccounts;
	RectI m_barArea, m_progress;
	RectI m_newsArea;

	int m_hover = -1;      // id do botao sob o mouse
	bool m_playDown = false;
	bool m_langOpen = false;
	int m_langHover = -1;

	std::vector<NewsItem> m_news;
	std::mutex m_newsMutex;

	int m_lastRevision = -1;
	bool m_errorShown = false;
};

}
