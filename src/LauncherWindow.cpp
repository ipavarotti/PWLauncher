#include "LauncherWindow.h"
#include "Common.h"
#include "Localization.h"
#include "Log.h"
#include <windowsx.h>
#include <shellapi.h>
#include <cstdio>
#include <cmath>
#include <cstring>

using namespace Gdiplus;

namespace el {

static const wchar_t* kClass = L"PWLauncherClass";

enum ButtonId
{
	ID_NONE = 0, ID_PLAY, ID_MIN, ID_CLOSE,
	ID_WEBSITE, ID_PROFILE, ID_DISCORD, ID_CHECK, ID_VERIFY, ID_LANG, ID_ACCOUNTS
};

// cores no estilo da referencia
static const DWORD C_WHITE      = 0xFFFFFFFF;
static const DWORD C_WHITE_CC   = 0xCCFFFFFF;
static const DWORD C_WHITE_EE   = 0xEEFFFFFF;
static const DWORD C_BLUE       = 0xFF4A90E2;
static const DWORD C_LINK       = 0xFF5BA1F3;

static const wchar_t* kFont = L"Segoe UI";

static const wchar_t* kLangCodes[] = { L"en", L"ru", L"uk", L"pt", L"es" };
static const wchar_t* kLangNames[] = {
	L"English", L"\u0420\u0443\u0441\u0441\u043a\u0438\u0439",
	L"\u0423\u043a\u0440\u0430\u0457\u043d\u0441\u044c\u043a\u0430",
	L"Portugu\u00eas", L"Espa\u00f1ol"
};
static const int kLangCount = 5;

LauncherWindow::LauncherWindow(AppConfig& cfg, UpdateEngine& engine)
	: m_cfg(cfg), m_engine(engine)
{
}

LauncherWindow::~LauncherWindow()
{
	if (m_acctHwnd) { DestroyWindow(m_acctHwnd); m_acctHwnd = nullptr; }
	delete m_bg;
	delete m_logo;
}

Gdiplus::Image* LauncherWindow::LoadImage(const std::wstring& rel)
{
	if (rel.empty()) return nullptr;
	std::wstring full = rel;
	size_t colon = rel.find(L':');
	if (colon == std::wstring::npos || colon != 1)
		full = JoinPath(m_cfg.exeDir, rel);
	Image* img = Image::FromFile(full.c_str());
	if (img && img->GetLastStatus() != Ok) { delete img; return nullptr; }
	return img;
}

void LauncherWindow::LoadImages()
{
	m_bg = LoadImage(L"img\\bg.png");
	m_logo = LoadImage(L"img\\logo.png");
}

int LauncherWindow::Measure(const std::wstring& text, int size, bool bold)
{
	Bitmap bmp(1, 1, PixelFormat32bppARGB);
	Graphics g(&bmp);
	FontFamily ff(kFont);
	Font font(&ff, (REAL)size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
	StringFormat fmt(StringFormat::GenericTypographic());
	RectF out;
	g.MeasureString(text.c_str(), -1, &font, PointF(0, 0), &fmt, &out);
	return (int)ceil(out.Width);
}

void LauncherWindow::LoadLayout()
{
	m_width = 800; m_height = 500;
	m_drag = { 0, 0, m_width, 40 };

	int logoW = m_logo ? 42 : 0;
	int navX = 15 + (m_logo ? logoW + 10 : 0);
	int navY = 7, navH = 26;

	auto nav = [&](const wchar_t* key, RectI& r)
	{
		std::wstring t = Localization::Get(key);
		int w = Measure(t, 10, false) + 14;
		r = { navX, navY, w, navH };
		navX += w + 4;
	};
	nav(L"ui.website", m_btnWebsite);
	nav(L"ui.profile", m_btnProfile);
	nav(L"ui.discord", m_btnDiscord);
	nav(L"ui.verify_files", m_btnVerify);
	nav(L"ui.check_updates", m_btnCheck);

	int pillW = 62, pillH = 28;
	int pillX = m_width - 15 - pillW;
	m_min = { pillX + 6, 6 + (pillH - 22) / 2, 22, 22 };
	m_close = { pillX + 34, 6 + (pillH - 22) / 2, 22, 22 };

	int langW = 120, langH = 24;
	m_langCombo = { pillX - 12 - langW, 8, langW, langH };

	// Botao "Contas" (estilo do Jogar), logo a esquerda do seletor de idioma.
	int acctW = 104, acctH = 24;
	m_btnAccounts = { m_langCombo.x - 10 - acctW, 8, acctW, acctH };

	m_play = { m_width - 24 - 120, m_height - 86 + 21, 120, 44 };
	m_barArea = { 0, m_height - 86, m_width, 86 };
	m_progress = { 24, m_height - 86 + 54, 608, 6 };
	m_newsArea = { m_width - 16 - 340, 48, 340, m_height - 86 - 48 - 8 };

	LayoutAccounts();
}

bool LauncherWindow::Create(HINSTANCE hInst)
{
	m_hInst = hInst;
	LoadImages();
	LoadLayout();
	LoadAccounts();

	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = &LauncherWindow::WndProc;
	wc.hInstance = hInst;
	wc.lpszClassName = kClass;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hIcon = LoadIcon(hInst, L"IDI_APP");
	RegisterClassExW(&wc);

	int sw = GetSystemMetrics(SM_CXSCREEN);
	int sh = GetSystemMetrics(SM_CYSCREEN);
	int x = (sw - m_width) / 2;
	int y = (sh - m_height) / 2;

	m_hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, kClass, L"Launcher",
		WS_POPUP, x, y, m_width, m_height, nullptr, nullptr, hInst, this);
	if (!m_hwnd) return false;

	SetTimer(m_hwnd, 1, 33, nullptr);
	return true;
}

void LauncherWindow::ShowError(const std::wstring& msg)
{
	MessageBoxW(m_hwnd, msg.c_str(), L"Launcher", MB_OK | MB_ICONERROR);
}

void LauncherWindow::SetNews(std::vector<NewsItem> items)
{
	{
		std::lock_guard<std::mutex> lk(m_newsMutex);
		m_news = std::move(items);
	}
	if (m_hwnd) PostMessageW(m_hwnd, WM_APP + 2, 0, 0);
}

// --------------------------------------------------------------------------- accounts

void LauncherWindow::LoadAccounts()
{
	if (!m_accounts.Load(m_cfg.elementDir))
		Log::Write(L"nao foi possivel carregar as contas (userdata\\accounts.dat)");
	LayoutAccounts();
}

void LauncherWindow::SaveAccounts()
{
	if (!m_accounts.Save())
		Log::Write(L"nao foi possivel salvar as contas (accounts.dat)");
}

void LauncherWindow::LayoutAccounts()
{
	m_acctPanel = { 12, 12, m_acctWinW - 24, m_acctWinH - 24 };
	ComputeAccountRects();
}

void LauncherWindow::ComputeAccountRects()
{
	const RectI& p = m_acctPanel;
	int pillW = 62, pillH = 28;
	int pillX = p.x + p.w - 12 - pillW;
	m_acctPill  = { pillX, p.y + 9, pillW, pillH };
	m_acctMin   = { pillX + 6,  p.y + 9 + (pillH - 22) / 2, 22, 22 };
	m_acctClose = { pillX + 34, p.y + 9 + (pillH - 22) / 2, 22, 22 };
	m_acctAdd   = { pillX - 8 - 26, p.y + 10, 26, 26 };
	m_acctDrag  = { p.x, p.y, p.w, 46 };

	m_acctRows.clear();
	m_acctRemove.clear();
	m_acctEdit.clear();

	int rowH = 48, gap = 6, pad = 10;
	int y = p.y + 46;
	for (size_t i = 0; i < m_accounts.Count(); ++i)
	{
		RectI row = { p.x + pad, y, p.w - pad * 2, rowH };
		m_acctRows.push_back({ row.x, row.y, row.w - 56, row.h });
		m_acctEdit.push_back({ row.x + row.w - 50, row.y + (rowH - 20) / 2, 22, 20 });
		m_acctRemove.push_back({ row.x + row.w - 26, row.y + (rowH - 20) / 2, 22, 20 });
		y += rowH + gap;
	}

	int fx = p.x + 40;
	int fw = p.w - 80;
	int fy = p.y + 66;
	m_formFields.assign(3, RectI());
	for (int k = 0; k < 3; ++k)
	{
		m_formFields[k] = { fx, fy, fw, 32 };
		fy += 32 + 36;
	}
	m_acctSave = { p.x + p.w / 2 - 102, fy + 6, 94, 34 };
	m_acctCancel = { p.x + p.w / 2 + 8, fy + 6, 94, 34 };
}

void LauncherWindow::OpenAccountForm(int index)
{
	m_acctFormOpen = true;
	m_acctEditIndex = index;
	if (index >= 0 && index < (int)m_accounts.Count())
	{
		const Account& a = m_accounts.Get(index);
		m_fLogin = a.login;
		m_fPassword = a.password;
		m_fRole = a.role;
	}
	else
	{
		m_fLogin.clear();
		m_fPassword.clear();
		m_fRole.clear();
	}
	m_formFocus = 0;
	m_acctHover = -1;
	m_acctItemHover = -1;
}

void LauncherWindow::CloseAccountForm()
{
	m_acctFormOpen = false;
	m_acctEditIndex = -1;
	m_formFocus = 0;
}

void LauncherWindow::CommitAccountForm()
{
	std::wstring login = Trim(m_fLogin);
	if (login.empty()) { m_formFocus = 0; Render(); return; }

	Account a;
	a.login = login;
	a.password = m_fPassword;
	a.role = Trim(m_fRole);

	if (m_acctEditIndex >= 0) m_accounts.Set((size_t)m_acctEditIndex, a);
	else                      m_accounts.Add(a);

	SaveAccounts();
	CloseAccountForm();
	LayoutAccounts();
	RenderAccounts();
}

void LauncherWindow::FocusFormField(int index)
{
	if (index < 0 || index > 2) return;
	m_formFocus = index;
	RenderAccounts();
}

static std::wstring MaskText(const std::wstring& s)
{
	return std::wstring(s.size(), L'\u2022');
}

void LauncherWindow::DrawAccountForm(Graphics& g)
{
	const RectI& p = m_acctPanel;
	FillRound(g, p, 10, 0xFA10121A);
	StrokeRound(g, p, 10, 0x44FFFFFF, 1.0f);

	std::wstring title = (m_acctEditIndex >= 0)
		? Localization::Get(L"ui.account_edit")
		: Localization::Get(L"ui.account_new");
	DrawTextU(g, title, p.x + 16, p.y + 12, p.w - 32, 24, 15, true, C_WHITE);

	const std::wstring labels[3] = {
		Localization::Get(L"ui.account_login"),
		Localization::Get(L"ui.account_password"),
		Localization::Get(L"ui.account_character")
	};
	const std::wstring* values[3] = { &m_fLogin, &m_fPassword, &m_fRole };

	for (int k = 0; k < 3; ++k)
	{
		const RectI& f = m_formFields[k];
		DrawTextU(g, labels[k], f.x, f.y - 20, f.w, 18, 11, false, C_WHITE_CC);
		bool focused = (m_formFocus == k);
		FillRound(g, f, 6, 0x33FFFFFF);
		StrokeRound(g, f, 6, focused ? C_BLUE : 0x44FFFFFF, focused ? 1.6f : 1.0f);

		std::wstring shown = (k == 1) ? MaskText(*values[k]) : *values[k];
		DrawTextU(g, shown, f.x + 10, f.y, f.w - 20, f.h, 13, false, C_WHITE);

		if (focused)
		{
			int tw = Measure(shown, 13, false);
			int cx = f.x + 12 + tw;
			if (cx > f.x + f.w - 12) cx = f.x + f.w - 12;
			Pen pen(Color(C_WHITE), 1.4f);
			g.DrawLine(&pen, cx, f.y + 7, cx, f.y + f.h - 7);
		}
	}

	{
		bool hover = (m_acctItemHover == 2);
		LinearGradientBrush grad(Point(m_acctSave.x, m_acctSave.y), Point(m_acctSave.x + m_acctSave.w, m_acctSave.y + m_acctSave.h),
			hover ? Color(0xFF6BB0E5) : Color(0xFF5A9FD4), hover ? Color(0xFF5BA1F3) : Color(0xFF4A90E2));
		GraphicsPath sp;
		int d = 16;
		sp.AddArc(m_acctSave.x, m_acctSave.y, d, d, 180, 90);
		sp.AddArc(m_acctSave.x + m_acctSave.w - d, m_acctSave.y, d, d, 270, 90);
		sp.AddArc(m_acctSave.x + m_acctSave.w - d, m_acctSave.y + m_acctSave.h - d, d, d, 0, 90);
		sp.AddArc(m_acctSave.x, m_acctSave.y + m_acctSave.h - d, d, d, 90, 90);
		sp.CloseFigure();
		g.FillPath(&grad, &sp);
		DrawTextU(g, Localization::Get(L"ui.save"), m_acctSave.x, m_acctSave.y, m_acctSave.w, m_acctSave.h, 13, true, C_WHITE, 1);
	}
	{
		bool hover = (m_acctItemHover == 3);
		FillRound(g, m_acctCancel, 8, hover ? 0x33FFFFFF : 0x22FFFFFF);
		StrokeRound(g, m_acctCancel, 8, 0x55FFFFFF, 1.0f);
		DrawTextU(g, Localization::Get(L"ui.cancel"), m_acctCancel.x, m_acctCancel.y, m_acctCancel.w, m_acctCancel.h, 13, false, C_WHITE, 1);
	}
}

void LauncherWindow::DrawAccounts(Graphics& g)
{
	if (m_acctFormOpen) { DrawAccountForm(g); return; }

	const RectI& p = m_acctPanel;
	FillRound(g, p, 12, 0xFC0F0F19);
	StrokeRound(g, p, 12, 0x44FFFFFF, 1.0f);

	DrawTextU(g, Localization::Get(L"ui.accounts"), p.x + 16, p.y + 10, 240, 26, 15, true, C_WHITE);

	{
		bool hover = (m_acctHover == -2);
		FillRound(g, m_acctAdd, 6, hover ? 0x33FFFFFF : 0x22FFFFFF);
		StrokeRound(g, m_acctAdd, 6, 0x55FFFFFF, 1.0f);
		DrawTextU(g, L"+", m_acctAdd.x, m_acctAdd.y, m_acctAdd.w, m_acctAdd.h, 18, true,
			hover ? C_WHITE : C_WHITE_EE, 1);
	}
	{
		FillRound(g, m_acctPill, 6, 0x22FFFFFF);
		StrokeRound(g, m_acctPill, 6, 0x55FFFFFF, 1.0f);
		FillRound(g, m_acctMin, 4, m_acctHover == -4 ? 0x22FFFFFF : 0x00000000);
		FillRound(g, m_acctClose, 4, m_acctHover == -3 ? 0x33FF5F56 : 0x00000000);
		DrawTextU(g, L"\u2013", m_acctMin.x, m_acctMin.y, m_acctMin.w, m_acctMin.h, 13, true, C_WHITE, 1);
		DrawTextU(g, L"\u00D7", m_acctClose.x, m_acctClose.y, m_acctClose.w, m_acctClose.h, 14, true, C_WHITE, 1);
	}

	if (m_accounts.Count() == 0)
	{
		DrawTextU(g, Localization::Get(L"ui.account_hint_empty") + L"\n" + Localization::Get(L"ui.account_hint_pw"),
			p.x + 26, p.y + 90, p.w - 52, 80, 12, false, C_WHITE_CC, 1, true);
		return;
	}

	for (size_t i = 0; i < m_accounts.Count(); ++i)
	{
		RectI row = { m_acctRows[i].x, m_acctRows[i].y, m_acctRows[i].w + 56, m_acctRows[i].h };
		bool hover = (m_acctHover == (int)i);
		FillRound(g, row, 8, hover ? 0x33FFFFFF : 0x1AFFFFFF);
		StrokeRound(g, row, 8, 0x22FFFFFF, 1.0f);

		const Account& a = m_accounts.Get(i);
		DrawTextU(g, a.login, row.x + 14, row.y + 6, row.w - 72, 22, 13, true, C_WHITE);
		std::wstring sub = a.role.empty()
			? Localization::Get(L"ui.click_to_login")
			: a.role;
		DrawTextU(g, sub, row.x + 14, row.y + 27, row.w - 72, 16, 11, false, C_WHITE_CC);

		DrawTextU(g, L"\u270E", m_acctEdit[i].x, m_acctEdit[i].y, m_acctEdit[i].w, m_acctEdit[i].h, 13, false, C_WHITE_CC, 1);
		DrawTextU(g, L"\u00D7", m_acctRemove[i].x, m_acctRemove[i].y, m_acctRemove[i].w, m_acctRemove[i].h, 15, false, C_WHITE_CC, 1);
	}
}

// --------------------------------------------------------------------------- accounts window

bool LauncherWindow::CreateAccountsWindow()
{
	if (m_acctHwnd) return true;

	WNDCLASSEXW wc = { 0 };
	wc.cbSize = sizeof(wc);
	wc.lpfnWndProc = &LauncherWindow::AccountsWndProc;
	wc.hInstance = m_hInst;
	wc.lpszClassName = L"PWLauncherAccountsClass";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	RegisterClassExW(&wc);

	int sw = GetSystemMetrics(SM_CXSCREEN);
	int sh = GetSystemMetrics(SM_CYSCREEN);
	int x = (sw - m_acctWinW) / 2;
	int y = (sh - m_acctWinH) / 2;

	m_acctHwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, L"PWLauncherAccountsClass",
		L"Contas", WS_POPUP, x, y, m_acctWinW, m_acctWinH, nullptr, nullptr, m_hInst, this);
	if (!m_acctHwnd) return false;

	LayoutAccounts();
	return true;
}

void LauncherWindow::ShowAccountsWindow()
{
	if (!CreateAccountsWindow()) return;
	LayoutAccounts();
	RenderAccounts();
	ShowWindow(m_acctHwnd, SW_SHOW);
	SetForegroundWindow(m_acctHwnd);
}

void LauncherWindow::RenderAccounts()
{
	if (!m_acctHwnd) return;

	HDC hScreen = GetDC(m_acctHwnd);
	HDC hMem = CreateCompatibleDC(hScreen);

	BITMAPINFOHEADER bi = { 0 };
	int bpl = ((m_acctWinW * 32 + 31) & ~31) >> 3;
	bi.biSize = sizeof(bi);
	bi.biWidth = m_acctWinW;
	bi.biHeight = m_acctWinH;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = bpl * m_acctWinH;

	void* bits = nullptr;
	HBITMAP hbmp = CreateDIBSection(nullptr, (PBITMAPINFO)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbmp) { DeleteDC(hMem); ReleaseDC(m_acctHwnd, hScreen); return; }
	HGDIOBJ old = SelectObject(hMem, hbmp);
	if (bits) memset(bits, 0, (size_t)bpl * m_acctWinH);

	{
		Graphics g(hMem);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
		g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
		DrawAccounts(g);
	}

	POINT ptSrc = { 0, 0 };
	SIZE sizeWnd = { m_acctWinW, m_acctWinH };
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	UpdateLayeredWindow(m_acctHwnd, hScreen, nullptr, &sizeWnd, hMem, &ptSrc, 0, &bf, ULW_ALPHA);

	SelectObject(hMem, old);
	DeleteObject(hbmp);
	DeleteDC(hMem);
	ReleaseDC(m_acctHwnd, hScreen);
}

LRESULT CALLBACK LauncherWindow::AccountsWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	LauncherWindow* self = nullptr;
	if (msg == WM_CREATE)
	{
		CREATESTRUCT* cs = (CREATESTRUCT*)lp;
		self = (LauncherWindow*)cs->lpCreateParams;
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
	}
	else self = (LauncherWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	if (self) return self->OnAccountsMessage(hWnd, msg, wp, lp);
	return DefWindowProcW(hWnd, msg, wp, lp);
}

LRESULT LauncherWindow::OnAccountsMessage(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CREATE:
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_MOUSEMOVE:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
		TrackMouseEvent(&tme);
		int na = -1, ni = -1;
		if (m_acctFormOpen)
		{
			for (int k = 0; k < 3; ++k)
				if (Hit(m_formFields[k], p)) { ni = 10 + k; break; }
			if (ni < 0)
			{
				if (Hit(m_acctSave, p)) ni = 2;
				else if (Hit(m_acctCancel, p)) ni = 3;
			}
		}
		else
		{
			if (Hit(m_acctAdd, p)) na = -2;
			else if (Hit(m_acctClose, p)) na = -3;
			else if (Hit(m_acctMin, p)) na = -4;
			else for (size_t i = 0; i < m_acctRows.size(); ++i)
				if (Hit(m_acctRemove[i], p) || Hit(m_acctEdit[i], p) || Hit(m_acctRows[i], p))
				{ na = (int)i; break; }
		}
		if (na != m_acctHover || ni != m_acctItemHover)
		{
			m_acctHover = na; m_acctItemHover = ni;
			RenderAccounts();
		}
		return 0;
	}

	case WM_MOUSELEAVE:
		m_acctHover = -1; m_acctItemHover = -1; RenderAccounts();
		return 0;

	case WM_LBUTTONDOWN:
		SetFocus(hWnd);
		return 0;

	case WM_LBUTTONUP:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		if (m_acctFormOpen)
		{
			for (int k = 0; k < 3; ++k)
				if (Hit(m_formFields[k], p)) { FocusFormField(k); return 0; }
			if (Hit(m_acctSave, p)) { CommitAccountForm(); return 0; }
			if (Hit(m_acctCancel, p)) { CloseAccountForm(); RenderAccounts(); return 0; }
			return 0;
		}
		if (Hit(m_acctClose, p)) { ShowWindow(hWnd, SW_HIDE); return 0; }
		if (Hit(m_acctMin, p)) { ShowWindow(hWnd, SW_MINIMIZE); return 0; }
		if (Hit(m_acctAdd, p)) { OpenAccountForm(-1); RenderAccounts(); return 0; }
		for (size_t i = 0; i < m_acctRows.size(); ++i)
		{
			if (i < m_acctRemove.size() && Hit(m_acctRemove[i], p))
			{
				std::wstring m = Localization::Format(Localization::Get(L"ui.account_delete_confirm"),
					{ m_accounts.Get(i).login });
				if (MessageBoxW(hWnd, m.c_str(), L"Launcher", MB_YESNO | MB_ICONQUESTION) == IDYES)
				{
					m_accounts.Remove(i);
					SaveAccounts();
					LayoutAccounts();
					m_acctHover = -1; m_acctItemHover = -1;
				}
				RenderAccounts();
				return 0;
			}
			if (i < m_acctEdit.size() && Hit(m_acctEdit[i], p)) { OpenAccountForm((int)i); RenderAccounts(); return 0; }
			if (Hit(m_acctRows[i], p))
			{
				if (m_onLoginAccount) m_onLoginAccount((int)i);
				return 0;
			}
		}
		return 0;
	}

	case WM_CHAR:
	{
		if (!m_acctFormOpen) return 0;
		wchar_t c = (wchar_t)wp;
		if (c >= 32 && c != 127)
		{
			std::wstring* f = (m_formFocus == 0) ? &m_fLogin : (m_formFocus == 1 ? &m_fPassword : &m_fRole);
			if (f->size() < 64) { f->push_back(c); RenderAccounts(); }
		}
		return 0;
	}

	case WM_KEYDOWN:
	{
		if (!m_acctFormOpen) break;
		std::wstring* f = (m_formFocus == 0) ? &m_fLogin : (m_formFocus == 1 ? &m_fPassword : &m_fRole);
		switch (wp)
		{
		case VK_BACK:
			if (!f->empty()) { f->pop_back(); RenderAccounts(); }
			return 0;
		case VK_TAB:
			m_formFocus = (m_formFocus + 1) % 3; RenderAccounts(); return 0;
		case VK_RETURN:
			CommitAccountForm(); return 0;
		case VK_ESCAPE:
			CloseAccountForm(); RenderAccounts(); return 0;
		default:
			break;
		}
		break;
	}

	case WM_NCHITTEST:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ScreenToClient(hWnd, &p);
		if (Hit(m_acctAdd, p) || Hit(m_acctMin, p) || Hit(m_acctClose, p)) return HTCLIENT;
		if (Hit(m_acctDrag, p)) return HTCAPTION;
		return HTCLIENT;
	}

	case WM_CLOSE:
		CloseAccountForm();
		ShowWindow(hWnd, SW_HIDE);
		return 0;

	case WM_DESTROY:
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wp, lp);
}

// --------------------------------------------------------------------------- helpers

void LauncherWindow::FillRound(Graphics& g, const RectI& r, int radius, DWORD argb)
{
	GraphicsPath p;
	int d = radius * 2;
	if (d <= 0) { SolidBrush b{Color(argb)}; g.FillRectangle(&b, r.x, r.y, r.w, r.h); return; }
	p.AddArc(r.x, r.y, d, d, 180, 90);
	p.AddArc(r.x + r.w - d, r.y, d, d, 270, 90);
	p.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
	p.AddArc(r.x, r.y + r.h - d, d, d, 90, 90);
	p.CloseFigure();
	SolidBrush b{Color(argb)};
	g.FillPath(&b, &p);
}

void LauncherWindow::StrokeRound(Graphics& g, const RectI& r, int radius, DWORD argb, float w)
{
	GraphicsPath p;
	int d = radius * 2;
	p.AddArc(r.x, r.y, d, d, 180, 90);
	p.AddArc(r.x + r.w - d, r.y, d, d, 270, 90);
	p.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
	p.AddArc(r.x, r.y + r.h - d, d, d, 90, 90);
	p.CloseFigure();
	Pen pen(Color(argb), w);
	g.DrawPath(&pen, &p);
}

void LauncherWindow::DrawHLine(Graphics& g, int x, int y, int w, DWORD argb)
{
	Pen pen(Color(argb), 1.0f);
	g.DrawLine(&pen, x, y, x + w, y);
}

void LauncherWindow::DrawTextU(Graphics& g, const std::wstring& text, int x, int y, int w, int h,
	int size, bool bold, DWORD argb, int align, bool wrap)
{
	if (text.empty()) return;
	FontFamily ff(kFont);
	Font font(&ff, (REAL)size, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
	SolidBrush brush{Color(argb)};
	StringFormat fmt;
	fmt.SetAlignment(align == 1 ? StringAlignmentCenter : (align == 2 ? StringAlignmentFar : StringAlignmentNear));
	fmt.SetLineAlignment(StringAlignmentCenter);
	if (wrap) fmt.SetTrimming(StringTrimmingEllipsisCharacter);
	else fmt.SetFormatFlags(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
	RectF r((REAL)x, (REAL)y, (REAL)w, (REAL)h);
	g.DrawString(text.c_str(), -1, &font, r, &fmt, &brush);
}

// --------------------------------------------------------------------------- buttons

void LauncherWindow::DrawNavButton(Graphics& g, int id, const std::wstring& text)
{
	const RectI* r = nullptr;
	switch (id) { case ID_WEBSITE: r = &m_btnWebsite; break; case ID_PROFILE: r = &m_btnProfile; break;
	case ID_DISCORD: r = &m_btnDiscord; break; case ID_CHECK: r = &m_btnCheck; break; case ID_VERIFY: r = &m_btnVerify; break; }
	if (!r) return;
	bool hover = (m_hover == id);
	DWORD bg = hover ? 0x25FFFFFF : 0x12FFFFFF;
	DWORD br = hover ? 0x35FFFFFF : 0x25FFFFFF;
	FillRound(g, *r, 4, bg);
	StrokeRound(g, *r, 4, br, 1.0f);
	DrawTextU(g, text, r->x, r->y, r->w, r->h, 10, false, C_WHITE_EE, 1);
}

void LauncherWindow::DrawPlayButton(Graphics& g, bool enabled)
{
	const RectI& r = m_play;
	bool hover = (m_hover == ID_PLAY);
	LinearGradientBrush grad(Point(r.x, r.y), Point(r.x + r.w, r.y + r.h),
		hover ? Color(0xFF6BB0E5) : Color(0xFF5A9FD4), hover ? Color(0xFF5BA1F3) : Color(0xFF4A90E2));
	GraphicsPath p;
	int d = 16;
	p.AddArc(r.x, r.y, d, d, 180, 90);
	p.AddArc(r.x + r.w - d, r.y, d, d, 270, 90);
	p.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
	p.AddArc(r.x, r.y + r.h - d, d, d, 90, 90);
	p.CloseFigure();
	if (!enabled) { SolidBrush dim{Color(0x99, 0x4A, 0x90, 0xE2)}; g.FillPath(&dim, &p); }
	else g.FillPath(&grad, &p);
	DrawTextU(g, Localization::Get(L"ui.play"), r.x, r.y, r.w, r.h, 16, true, enabled ? C_WHITE : C_WHITE_CC, 1);
}

void LauncherWindow::DrawAccountsButton(Graphics& g)
{
	const RectI& r = m_btnAccounts;
	bool hover = (m_hover == ID_ACCOUNTS);
	LinearGradientBrush grad(Point(r.x, r.y), Point(r.x + r.w, r.y + r.h),
		hover ? Color(0xFF6BB0E5) : Color(0xFF5A9FD4), hover ? Color(0xFF5BA1F3) : Color(0xFF4A90E2));
	GraphicsPath p;
	int d = 12;
	p.AddArc(r.x, r.y, d, d, 180, 90);
	p.AddArc(r.x + r.w - d, r.y, d, d, 270, 90);
	p.AddArc(r.x + r.w - d, r.y + r.h - d, d, d, 0, 90);
	p.AddArc(r.x, r.y + r.h - d, d, d, 90, 90);
	p.CloseFigure();
	g.FillPath(&grad, &p);
	DrawTextU(g, Localization::Get(L"ui.accounts"), r.x, r.y, r.w, r.h, 12, true, C_WHITE, 1);
}

void LauncherWindow::DrawProgress(Graphics& g, const RectI& r, int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 100) percent = 100;
	FillRound(g, r, 6, 0x33FFFFFF);
	StrokeRound(g, r, 6, 0x55FFFFFF, 1.0f);
	FillRound(g, { r.x + 1, r.y + 1, r.w - 2, r.h - 2 }, 5, 0x22FFFFFF);
	int fw = (r.w - 2) * percent / 100;
	if (fw > 2) FillRound(g, { r.x + 1, r.y + 1, fw, r.h - 2 }, 5, C_BLUE);
}

// --------------------------------------------------------------------------- news

void LauncherWindow::DrawNews(Graphics& g)
{
	std::vector<NewsItem> items;
	{
		std::lock_guard<std::mutex> lk(m_newsMutex);
		items = m_news;
	}
	if (items.empty()) return;

	std::wstring lang = Localization::CurrentLanguage();
	int cardH = 80, gap = 8;
	int total = (int)items.size() * (cardH + gap);
	int y = m_newsArea.y + (m_newsArea.h - total) / 2;
	if (y < m_newsArea.y) y = m_newsArea.y;

	for (const NewsItem& n : items)
	{
		RectI card = { m_newsArea.x + 8, y, m_newsArea.w - 8, cardH };
		FillRound(g, card, 8, 0xE6101521);
		StrokeRound(g, card, 8, 0xFF1E2A3C, 1.0f);

		RectI img = { card.x + 8, card.y + 8, 64, 64 };
		FillRound(g, img, 7, 0xFF121826);

		int tx = card.x + 8 + 64 + 8;
		int tw = card.w - (tx - card.x) - 12;
		FillRound(g, { tx, card.y + 14, 3, 14 }, 1, C_BLUE);
		DrawTextU(g, n.title.Get(lang), tx + 9, card.y + 10, tw - 9, 18, 12, true, C_WHITE);
		DrawTextU(g, n.summary.Get(lang), tx, card.y + 32, tw, 26, 11, false, C_WHITE_CC, 0, true);
		DrawTextU(g, Localization::Get(L"ui.read_more") + L" \u2192", tx, card.y + 58, tw, 16, 10, false, C_LINK, 2);
		y += cardH + gap;
	}
}

// --------------------------------------------------------------------------- language

void LauncherWindow::DrawLangCombo(Graphics& g)
{
	const RectI& r = m_langCombo;
	bool hover = (m_hover == ID_LANG);
	FillRound(g, r, 4, (hover || m_langOpen) ? 0x22FFFFFF : 0x33FFFFFF);
	StrokeRound(g, r, 4, (hover || m_langOpen) ? 0x66FFFFFF : 0x55FFFFFF, 1.0f);
	std::wstring cur = Localization::CurrentLanguage();
	std::wstring name = L"English";
	for (int i = 0; i < kLangCount; ++i)
		if (cur == kLangCodes[i]) name = kLangNames[i];
	DrawTextU(g, name, r.x + 8, r.y, r.w - 24, r.h, 12, false, C_WHITE);
	{
		int cx = r.x + r.w - 14, cy = r.y + r.h / 2;
		PointF tri[3] = { PointF((REAL)cx - 4, (REAL)cy - 2), PointF((REAL)cx + 4, (REAL)cy - 2), PointF((REAL)cx, (REAL)cy + 3) };
		SolidBrush tb{Color(C_WHITE_CC)};
		g.FillPolygon(&tb, tri, 3);
	}
}

void LauncherWindow::DrawLangDropdown(Graphics& g)
{
	if (!m_langOpen) return;
	int rowH = 24;
	RectI box = { m_langCombo.x, m_langCombo.y + m_langCombo.h + 2, m_langCombo.w, rowH * kLangCount + 8 };
	FillRound(g, box, 4, 0xFF1E1E28);
	StrokeRound(g, box, 4, 0x33FFFFFF, 1.0f);
	for (int i = 0; i < kLangCount; ++i)
	{
		RectI row = { box.x + 4, box.y + 4 + i * rowH, box.w - 8, rowH };
		if (m_langHover == i) FillRound(g, row, 4, 0x22FFFFFF);
		DWORD col = (Localization::CurrentLanguage() == kLangCodes[i]) ? C_BLUE : C_WHITE;
		DrawTextU(g, kLangNames[i], row.x + 8, row.y, row.w - 12, row.h, 12, false, col);
	}
}

// --------------------------------------------------------------------------- render

void LauncherWindow::Render()
{
	if (!m_hwnd) return;

	HDC hScreen = GetDC(m_hwnd);
	HDC hMem = CreateCompatibleDC(hScreen);

	BITMAPINFOHEADER bi = { 0 };
	int bpl = ((m_width * 32 + 31) & ~31) >> 3;
	bi.biSize = sizeof(bi);
	bi.biWidth = m_width;
	bi.biHeight = m_height;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = bpl * m_height;

	void* bits = nullptr;
	HBITMAP hbmp = CreateDIBSection(nullptr, (PBITMAPINFO)&bi, DIB_RGB_COLORS, &bits, nullptr, 0);
	if (!hbmp) { DeleteDC(hMem); ReleaseDC(m_hwnd, hScreen); return; }
	HGDIOBJ old = SelectObject(hMem, hbmp);

	{
		Graphics g(hMem);
		g.SetSmoothingMode(SmoothingModeAntiAlias);
		g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
		g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

		// fundo
		if (m_bg)
			g.DrawImage(m_bg, 0, 0, m_width, m_height);
		else
		{
			LinearGradientBrush grad(Point(0, 0), Point(m_width, m_height),
				Color(0xFF0A0A0A), Color(0xFF16213E));
			g.FillRectangle(&grad, 0, 0, m_width, m_height);
		}

		// barra de titulo (gradiente)
		{
			LinearGradientBrush tp(Point(0, 0), Point(0, 40), Color(0xCC0B1424), Color(0xCC071834));
			g.FillRectangle(&tp, 0, 0, m_width, 40);
			DrawHLine(g, 0, 40, m_width, 0x33FFFFFF);
		}

		// logo
		if (m_logo)
			g.DrawImage(m_logo, 15, 5, 42, 32);

		// nav + idioma
		DrawNavButton(g, ID_WEBSITE, Localization::Get(L"ui.website"));
		DrawNavButton(g, ID_PROFILE, Localization::Get(L"ui.profile"));
		DrawNavButton(g, ID_DISCORD, Localization::Get(L"ui.discord"));
		DrawNavButton(g, ID_VERIFY, Localization::Get(L"ui.verify_files"));
		DrawNavButton(g, ID_CHECK, Localization::Get(L"ui.check_updates"));
		DrawAccountsButton(g);
		DrawLangCombo(g);

		// pill minimizar/fechar
		{
			RectI pill = { m_close.x + 22 + 6, 6, 0, 0 };
			pill.x = m_min.x - 6; pill.w = (m_close.x + 22 + 6) - pill.x; pill.h = 28;
			FillRound(g, pill, 4, 0x22FFFFFF);
			StrokeRound(g, pill, 4, 0x55FFFFFF, 1.0f);
			FillRound(g, m_min, 4, m_hover == ID_MIN ? 0x22FFFFFF : 0x00000000);
			FillRound(g, m_close, 4, m_hover == ID_CLOSE ? 0x33FF5F56 : 0x00000000);
			DrawTextU(g, L"\u2013", m_min.x, m_min.y, m_min.w, m_min.h, 13, true, C_WHITE, 1);
			DrawTextU(g, L"\u00D7", m_close.x, m_close.y, m_close.w, m_close.h, 14, true, C_WHITE, 1);
		}

		// noticias
		DrawNews(g);

		// barra inferior
		{
			FillRound(g, m_barArea, 0, 0xE60F0F19);
			DrawHLine(g, 0, m_barArea.y, m_width, 0x33FFFFFF);
		}

		UpdateStatus s = m_engine.Snapshot();

		DrawTextU(g, s.message.empty() ? Localization::Get(L"ui.ready_to_play") : s.message,
			24, m_barArea.y + 8, 608, 20, 14, false, C_WHITE);
		if (!s.speedText.empty())
			DrawTextU(g, s.speedText, 24, m_barArea.y + 32, 400, 16, 12, false, C_WHITE_CC);
		if (s.etaSeconds > 0)
		{
			std::wstring eta = Localization::Format(Localization::Get(L"ui.eta_only"),
				{ Format(L"%dm %ds", s.etaSeconds / 60, s.etaSeconds % 60) });
			DrawTextU(g, eta, 240, m_barArea.y + 32, 300, 16, 12, false, C_WHITE_CC);
		}

		DrawProgress(g, m_progress, s.totalPercent > 0 ? s.totalPercent : (s.canPlay ? 100 : 0));
		DrawTextU(g, Format(L"%d%%", s.totalPercent > 0 ? s.totalPercent : (s.canPlay ? 100 : 0)),
			m_progress.x + m_progress.w - 60, m_barArea.y + 30, 60, 18, 13, true, C_WHITE, 2);

		std::wstring ver = s.versionText.empty() ? L"" : s.versionText;
		DrawTextU(g, ver, 24, m_barArea.y + 66, 400, 16, 12, false, C_WHITE_CC);

		bool playEnabled = s.canPlay && !s.failed;
		DrawPlayButton(g, playEnabled);

		if (m_langOpen) DrawLangDropdown(g);
	}

	POINT ptSrc = { 0, 0 };
	SIZE sizeWnd = { m_width, m_height };
	BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
	UpdateLayeredWindow(m_hwnd, hScreen, nullptr, &sizeWnd, hMem, &ptSrc, 0, &bf, ULW_ALPHA);

	SelectObject(hMem, old);
	DeleteObject(hbmp);
	DeleteDC(hMem);
	ReleaseDC(m_hwnd, hScreen);
}

void LauncherWindow::Refresh() { Render(); }

int LauncherWindow::HitButton(POINT p) const
{
	if (Hit(m_play, p)) return ID_PLAY;
	if (Hit(m_min, p)) return ID_MIN;
	if (Hit(m_close, p)) return ID_CLOSE;
	if (Hit(m_btnWebsite, p)) return ID_WEBSITE;
	if (Hit(m_btnProfile, p)) return ID_PROFILE;
	if (Hit(m_btnDiscord, p)) return ID_DISCORD;
	if (Hit(m_btnAccounts, p)) return ID_ACCOUNTS;
	if (Hit(m_btnVerify, p)) return ID_VERIFY;
	if (Hit(m_btnCheck, p)) return ID_CHECK;
	if (Hit(m_langCombo, p)) return ID_LANG;
	return ID_NONE;
}

LRESULT CALLBACK LauncherWindow::WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	LauncherWindow* self = nullptr;
	if (msg == WM_CREATE)
	{
		CREATESTRUCT* cs = (CREATESTRUCT*)lp;
		self = (LauncherWindow*)cs->lpCreateParams;
		SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)self);
	}
	else self = (LauncherWindow*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
	if (self) return self->OnMessage(hWnd, msg, wp, lp);
	return DefWindowProcW(hWnd, msg, wp, lp);
}

LRESULT LauncherWindow::OnMessage(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
	switch (msg)
	{
	case WM_CREATE:
		Render();
		ShowWindow(hWnd, SW_SHOW);
		return 0;

	case WM_ERASEBKGND:
		return 1;

	case WM_TIMER:
	{
		UpdateStatus s = m_engine.Snapshot();
		if ((int)s.revision != m_lastRevision)
		{
			m_lastRevision = (int)s.revision;
			Render();
		}
		if (s.failed && !m_errorShown)
		{
			m_errorShown = true;
			ShowError(s.message);
		}
		return 0;
	}

	case WM_APP + 1:
	case WM_APP + 2:
		Render();
		return 0;

	case WM_MOUSEMOVE:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hWnd, 0 };
		TrackMouseEvent(&tme);
		int nh = HitButton(p);
		int nlh = -1;
		if (m_langOpen)
		{
			RectI box = { m_langCombo.x, m_langCombo.y + m_langCombo.h + 2, m_langCombo.w, 24 * kLangCount + 8 };
			for (int i = 0; i < kLangCount; ++i)
			{
				RectI row = { box.x + 4, box.y + 4 + i * 24, box.w - 8, 24 };
				if (Hit(row, p)) { nlh = i; break; }
			}
		}
		if (nh != m_hover || nlh != m_langHover)
		{
			m_hover = nh; m_langHover = nlh;
			Render();
		}
		return 0;
	}

	case WM_MOUSELEAVE:
		m_hover = -1; m_langHover = -1; Render();
		return 0;

	case WM_LBUTTONDOWN:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		SetFocus(hWnd);
		if (Hit(m_play, p)) { m_playDown = true; Render(); }
		return 0;
	}

	case WM_LBUTTONUP:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };

		if (m_langOpen)
		{
			RectI box = { m_langCombo.x, m_langCombo.y + m_langCombo.h + 2, m_langCombo.w, 24 * kLangCount + 8 };
			for (int i = 0; i < kLangCount; ++i)
			{
				RectI row = { box.x + 4, box.y + 4 + i * 24, box.w - 8, 24 };
				if (Hit(row, p))
				{
					Localization::SetLanguage(kLangCodes[i]);
					m_cfg.language = kLangCodes[i];
					if (m_cfg.hasConfigJson) m_cfg.SaveJsonConfig();
					m_langOpen = false;
					LoadLayout();
					Render();
					return 0;
				}
			}
			m_langOpen = false; Render();
			return 0;
		}

		int id = HitButton(p);
		switch (id)
		{
		case ID_PLAY:
			if (m_playDown)
			{
				m_playDown = false;
				UpdateStatus s = m_engine.Snapshot();
				if (s.canPlay && !s.failed && m_onPlay) m_onPlay();
			}
			Render();
			return 0;
		case ID_MIN:  ShowWindow(hWnd, SW_MINIMIZE); return 0;
		case ID_CLOSE: if (m_onClose) m_onClose(); PostMessageW(hWnd, WM_CLOSE, 0, 0); return 0;
		case ID_LANG: m_langOpen = true; Render(); return 0;
		case ID_ACCOUNTS: ShowAccountsWindow(); return 0;
		case ID_VERIFY: if (m_onVerify) m_onVerify(); return 0;
		case ID_CHECK: if (m_onCheckUpdates) m_onCheckUpdates(); return 0;
		case ID_WEBSITE: if (!m_cfg.linkWebsite.empty()) ShellExecuteW(nullptr, L"open", m_cfg.linkWebsite.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
		case ID_PROFILE: if (!m_cfg.linkProfile.empty()) ShellExecuteW(nullptr, L"open", m_cfg.linkProfile.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
		case ID_DISCORD: if (!m_cfg.linkDiscord.empty()) ShellExecuteW(nullptr, L"open", m_cfg.linkDiscord.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return 0;
		}
		m_playDown = false;
		return 0;
	}

	case WM_NCHITTEST:
	{
		POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
		ScreenToClient(hWnd, &p);
		if (HitButton(p) != ID_NONE) return HTCLIENT;
		if (Hit(m_drag, p)) return HTCAPTION;
		return HTCLIENT;
	}

	case WM_CLOSE:
		if (m_onClose) m_onClose();
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		KillTimer(hWnd, 1);
		if (m_acctHwnd) { DestroyWindow(m_acctHwnd); m_acctHwnd = nullptr; }
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(hWnd, msg, wp, lp);
}

}
