#include "GuildIconsUpdater.h"
#include "Common.h"
#include "HttpClient.h"
#include "PckPackage.h"
#include "Log.h"
#include <windows.h>
#include <gdiplus.h>
#include <zlib.h>
#include <vector>
#include <cstdio>

namespace el {

// ---------------------------------------------------------------------------
//  Codificador DDS / DXT5 (BC1 cor + alfa BC4), portado do launcher de referencia.
// ---------------------------------------------------------------------------

static void WriteDxt5AlphaBlock(std::vector<uint8_t>& out, const uint8_t* blockRGBA)
{
	uint8_t a0 = 255, a1 = 0;
	uint8_t alpha[16];
	for (int i = 0; i < 16; ++i)
	{
		uint8_t a = blockRGBA[i * 4 + 3];
		alpha[i] = a;
		if (a < a0) a0 = a;
		if (a > a1) a1 = a;
	}
	uint8_t hi = a1, lo = a0;
	if (hi == lo) lo = (hi == 0) ? 0 : (uint8_t)(hi - 1);

	int pal[8];
	pal[0] = hi; pal[1] = lo;
	if (hi > lo)
	{
		for (int i = 1; i <= 6; ++i)
			pal[i + 1] = ((7 - i) * hi + i * lo + 3) / 7;
	}
	else
	{
		for (int i = 1; i <= 4; ++i)
			pal[i + 1] = ((5 - i) * hi + i * lo + 2) / 5;
		pal[6] = 0;
		pal[7] = 255;
	}

	uint64_t bits = 0;
	for (int i = 0; i < 16; ++i)
	{
		int v = alpha[i];
		int best = 0, bestErr = INT32_MAX;
		for (int j = 0; j < 8; ++j)
		{
			int d = v - pal[j];
			int e = d * d;
			if (e < bestErr) { bestErr = e; best = j; }
		}
		bits |= (uint64_t)(best & 7) << (3 * i);
	}

	out.push_back(hi);
	out.push_back(lo);
	for (int i = 0; i < 6; ++i)
		out.push_back((uint8_t)((bits >> (8 * i)) & 0xFF));
}

static uint16_t PackRgb565(int r, int g, int b)
{
	return (uint16_t)(((r & 0xFF) >> 3 << 11) | ((g & 0xFF) >> 2 << 5) | ((b & 0xFF) >> 3));
}

static void UnpackRgb565(uint16_t c, int& r, int& g, int& b)
{
	int r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
	r = (r5 << 3) | (r5 >> 2);
	g = (g6 << 2) | (g6 >> 4);
	b = (b5 << 3) | (b5 >> 2);
}

static void WriteBc1ColorBlock(std::vector<uint8_t>& out, const uint8_t* blockRGBA)
{
	int r1 = 255, g1 = 255, b1 = 255, r2 = 0, g2 = 0, b2 = 0;
	for (int i = 0; i < 16; ++i)
	{
		int r = blockRGBA[i * 4], g = blockRGBA[i * 4 + 1], b = blockRGBA[i * 4 + 2];
		if (r < r1) r1 = r;
		if (r > r2) r2 = r;
		if (g < g1) g1 = g;
		if (g > g2) g2 = g;
		if (b < b1) b1 = b;
		if (b > b2) b2 = b;
	}
	uint16_t c0 = PackRgb565(r2, g2, b2);
	uint16_t c1 = PackRgb565(r1, g1, b1);
	if (c0 == c1) c1 ^= 1;
	if (c0 < c1) { uint16_t t = c0; c0 = c1; c1 = t; }

	int r0, g0, b0, r3, g3, b3;
	UnpackRgb565(c0, r0, g0, b0);
	UnpackRgb565(c1, r3, g3, b3);
	int col[4][3] = {
		{ r0, g0, b0 },
		{ r3, g3, b3 },
		{ (2 * r0 + r3) / 3, (2 * g0 + g3) / 3, (2 * b0 + b3) / 3 },
		{ (r0 + 2 * r3) / 3, (g0 + 2 * g3) / 3, (b0 + 2 * b3) / 3 }
	};

	uint32_t idx = 0;
	for (int i = 0; i < 16; ++i)
	{
		int r = blockRGBA[i * 4], g = blockRGBA[i * 4 + 1], b = blockRGBA[i * 4 + 2];
		int best = 0, bestErr = INT32_MAX;
		for (int j = 0; j < 4; ++j)
		{
			int dr = r - col[j][0], dg = g - col[j][1], db = b - col[j][2];
			int e = dr * dr + dg * dg + db * db;
			if (e < bestErr) { bestErr = e; best = j; }
		}
		idx |= (uint32_t)(best & 3) << (2 * i);
	}

	out.push_back((uint8_t)(c0 & 0xFF));
	out.push_back((uint8_t)(c0 >> 8));
	out.push_back((uint8_t)(c1 & 0xFF));
	out.push_back((uint8_t)(c1 >> 8));
	for (int i = 0; i < 4; ++i)
		out.push_back((uint8_t)((idx >> (8 * i)) & 0xFF));
}

static void WriteU32(std::vector<uint8_t>& out, uint32_t v)
{
	out.push_back(v & 0xFF);
	out.push_back((v >> 8) & 0xFF);
	out.push_back((v >> 16) & 0xFF);
	out.push_back((v >> 24) & 0xFF);
}

// rgba: width*height*4, ordem R,G,B,A, top-down.
static std::vector<uint8_t> EncodeDdsDxt5(int width, int height, const std::vector<uint8_t>& rgba)
{
	std::vector<uint8_t> out;
	int bx = (width + 3) / 4;
	int by = (height + 3) / 4;
	out.reserve(128 + bx * by * 16);

	WriteU32(out, 542327876);   // 'DDS '
	WriteU32(out, 124);         // header size
	WriteU32(out, 528391);      // flags
	WriteU32(out, (uint32_t)height);
	WriteU32(out, (uint32_t)width);
	WriteU32(out, (uint32_t)(bx * by * 16));
	WriteU32(out, 0);           // depth
	WriteU32(out, 0);           // mipmaps
	for (int i = 0; i < 11; ++i) WriteU32(out, 0);
	WriteU32(out, 32);          // pf size
	WriteU32(out, 4);           // pf flags: DDPF_FOURCC
	WriteU32(out, 894720068);   // 'DXT5'
	WriteU32(out, 0); WriteU32(out, 0); WriteU32(out, 0); WriteU32(out, 0); WriteU32(out, 0);
	WriteU32(out, 4096);        // caps
	WriteU32(out, 0); WriteU32(out, 0); WriteU32(out, 0); WriteU32(out, 0);

	uint8_t block[64];
	for (int bY = 0; bY < by; ++bY)
	{
		for (int bX = 0; bX < bx; ++bX)
		{
			for (int py = 0; py < 4; ++py)
			{
				int sy = bY * 4 + py; if (sy >= height) sy = height - 1;
				for (int px = 0; px < 4; ++px)
				{
					int sx = bX * 4 + px; if (sx >= width) sx = width - 1;
					int si = (sy * width + sx) * 4;
					int di = (py * 4 + px) * 4;
					block[di] = rgba[si];
					block[di + 1] = rgba[si + 1];
					block[di + 2] = rgba[si + 2];
					block[di + 3] = rgba[si + 3];
				}
			}
			WriteDxt5AlphaBlock(out, block);
			WriteBc1ColorBlock(out, block);
		}
	}
	return out;
}

// Extrai RGBA top-down a partir de um PNG/SVG em memoria via GDI+.
static bool DecodeImageToRgba(const std::vector<uint8_t>& fileBytes, std::vector<uint8_t>& rgba, int& w, int& h)
{
	ULONG_PTR token = 0;
	Gdiplus::GdiplusStartupInput input;
	bool started = (Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok);

	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, fileBytes.size());
	if (!mem) { if (started) Gdiplus::GdiplusShutdown(token); return false; }
	void* p = GlobalLock(mem);
	memcpy(p, fileBytes.data(), fileBytes.size());
	GlobalUnlock(mem);

	IStream* stream = nullptr;
	bool ok = false;
	if (CreateStreamOnHGlobal(mem, TRUE, &stream) == S_OK)
	{
		Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromStream(stream);
		if (bmp && bmp->GetLastStatus() == Gdiplus::Ok)
		{
			w = (int)bmp->GetWidth();
			h = (int)bmp->GetHeight();
			Gdiplus::Rect rect(0, 0, w, h);
			Gdiplus::BitmapData bd;
			if (bmp->LockBits(&rect, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) == Gdiplus::Ok)
			{
				rgba.assign((size_t)w * h * 4, 0);
				for (int y = 0; y < h; ++y)
				{
					const uint8_t* row = (const uint8_t*)bd.Scan0 + (ptrdiff_t)y * bd.Stride;
					uint8_t* dst = rgba.data() + (size_t)y * w * 4;
					for (int x = 0; x < w; ++x)
					{
						// memoria GDI+ 32bpp ARGB = B,G,R,A
						dst[x * 4] = row[x * 4 + 2];
						dst[x * 4 + 1] = row[x * 4 + 1];
						dst[x * 4 + 2] = row[x * 4 + 0];
						dst[x * 4 + 3] = row[x * 4 + 3];
					}
				}
				bmp->UnlockBits(&bd);
				ok = true;
			}
			delete bmp;
		}
		stream->Release();
	}
	if (started) Gdiplus::GdiplusShutdown(token);
	return ok;
}

static std::vector<uint8_t> MakePacked(const std::vector<uint8_t>& raw)
{
	uLongf compLen = compressBound((uLong)raw.size());
	std::vector<uint8_t> comp(compLen);
	if (compress2(comp.data(), &compLen, raw.data(), (uLong)raw.size(), 9) != Z_OK)
		return std::vector<uint8_t>();
	comp.resize(compLen);

	std::vector<uint8_t> packed(4 + comp.size());
	uint32_t orig = (uint32_t)raw.size();
	packed[0] = orig & 0xFF; packed[1] = (orig >> 8) & 0xFF;
	packed[2] = (orig >> 16) & 0xFF; packed[3] = (orig >> 24) & 0xFF;
	memcpy(packed.data() + 4, comp.data(), comp.size());
	return packed;
}

// ---------------------------------------------------------------------------

GuildIconsUpdater::GuildIconsUpdater(AppConfig& cfg, LogFn log)
	: m_cfg(cfg), m_log(std::move(log))
{
}

std::wstring GuildIconsUpdater::CacheBust(const std::wstring& url) const
{
	if (url.empty()) return url;
	wchar_t r[16];
	swprintf(r, 16, L"%d", 100000 + (GetTickCount() % 900000));
	std::wstring s = url;
	if (s.find(L"?") == std::wstring::npos) return s + L"?r=" + r;
	if (s.size() >= 2 && s.substr(s.size() - 2) == L"r=") return s + r;
	return s + L"&r=" + r;
}

bool GuildIconsUpdater::Run()
{
	if (m_cfg.clanVersionUrl.empty() || m_cfg.clanPngUrl.empty() || m_cfg.clanTxtUrl.empty())
		return false;

	std::wstring pckPath = JoinPath(m_cfg.elementDir, L"surfaces.pck");
	if (!FileExists(pckPath))
	{
		if (m_log) m_log(L"[GuildIcons] surfaces.pck nao encontrado: " + pckPath);
		return false;
	}

	std::wstring tempRoot = JoinPath(m_cfg.elementDir, L"_temp");
	MakeDirs(tempRoot);

	HttpClient http;
	http.SetTimeouts(5000, 30000);
	http.SetUserAgent(L"PWLauncher/GuildIcons");

	// 1) versao remota
	std::wstring verFile = JoinPath(tempRoot, L"guild.version");
	DWORD st = 0;
	if (!http.Download(CacheBust(m_cfg.clanVersionUrl), verFile, nullptr, 0, &st))
	{
		if (m_log) m_log(Format(L"[GuildIcons] version indisponivel (HTTP %u)", st));
		return false;
	}
	std::string verText; ReadFileUtf8(verFile, verText); DeleteFileW(verFile.c_str());
	std::wstring remoteVer = Trim(Widen(verText, CP_UTF8));
	if (!remoteVer.empty() && ToLower(remoteVer) == ToLower(m_cfg.clanVersion))
	{
		if (m_log) m_log(L"[GuildIcons] atualizado (v" + remoteVer + L")");
		return false;
	}

	// 2) PNG -> DDS
	std::wstring pngFile = JoinPath(tempRoot, L"iconlist_guild.png");
	if (!http.Download(CacheBust(m_cfg.clanPngUrl), pngFile, nullptr, 0, &st))
	{
		if (m_log) m_log(Format(L"[GuildIcons] png indisponivel (HTTP %u)", st));
		return false;
	}
	std::vector<uint8_t> pngBytes;
	ReadFileBytes(pngFile, pngBytes);
	DeleteFileW(pngFile.c_str());

	std::vector<uint8_t> rgba; int w = 0, h = 0;
	if (!DecodeImageToRgba(pngBytes, rgba, w, h))
	{
		if (m_log) m_log(L"[GuildIcons] falha ao decodificar o PNG");
		return false;
	}
	std::vector<uint8_t> dds = EncodeDdsDxt5(w, h, rgba);

	// 3) TXT
	std::wstring txtFile = JoinPath(tempRoot, L"iconlist_guild.txt");
	if (!http.Download(CacheBust(m_cfg.clanTxtUrl), txtFile, nullptr, 0, &st))
	{
		if (m_log) m_log(Format(L"[GuildIcons] txt indisponivel (HTTP %u)", st));
		return false;
	}
	std::vector<uint8_t> txtBytes;
	ReadFileBytes(txtFile, txtBytes);
	DeleteFileW(txtFile.c_str());

	// 4) grava em surfaces.pck
	pck::PckPackage pkg;
	if (!pkg.Open(pckPath, false))
	{
		if (m_log) m_log(L"[GuildIcons] falha ao abrir surfaces.pck");
		return false;
	}
	std::vector<uint8_t> ddsPacked = MakePacked(dds);
	std::vector<uint8_t> txtPacked = MakePacked(txtBytes);
	bool ok = !ddsPacked.empty() && !txtPacked.empty()
		&& pkg.AddOrReplacePacked("surfaces\\iconset\\iconlist_guild.dds", ddsPacked.data(), ddsPacked.size())
		&& pkg.AddOrReplacePacked("surfaces\\iconset\\iconlist_guild.txt", txtPacked.data(), txtPacked.size());
	ok = pkg.Close() && ok;

	if (!ok)
	{
		if (m_log) m_log(L"[GuildIcons] falha ao gravar em surfaces.pck");
		return false;
	}

	// 5) persiste a versao
	if (!remoteVer.empty())
	{
		m_cfg.clanVersion = remoteVer;
		if (m_cfg.hasConfigJson) m_cfg.SaveJsonConfig();
	}
	if (m_log) m_log(L"[GuildIcons] aplicado (v" + remoteVer + L")");
	return true;
}

}
