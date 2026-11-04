#include "NewsService.h"
#include "Common.h"
#include "Json.h"
#include "HttpClient.h"

namespace el {

static std::wstring CombineUrl(const std::wstring& base, const std::wstring& rel)
{
	if (rel.find(L"http://") == 0 || rel.find(L"https://") == 0) return rel;
	if (base.empty()) return rel;
	std::wstring b = base;
	if (b.back() != L'/') b += L'/';
	std::wstring r = rel;
	while (!r.empty() && (r.front() == L'/' || r.front() == L'\\')) r.erase(r.begin());
	return b + r;
}

static TranslatedString ParseTranslated(const json::Value& v)
{
	TranslatedString t;
	if (!v.IsObject()) return t;
	t.ru = Widen(v.GetString("ru"), CP_UTF8);
	t.en = Widen(v.GetString("en"), CP_UTF8);
	t.uk = Widen(v.GetString("uk"), CP_UTF8);
	t.pt = Widen(v.GetString("pt"), CP_UTF8);
	t.es = Widen(v.GetString("es"), CP_UTF8);
	return t;
}

NewsService::NewsService(const std::wstring& baseUrl, std::function<void(const std::wstring&)> log)
	: m_baseUrl(baseUrl), m_log(std::move(log))
{
	while (!m_baseUrl.empty() && m_baseUrl.back() == L'/') m_baseUrl.pop_back();
}

std::vector<NewsItem> NewsService::LoadTop(int count)
{
	std::vector<NewsItem> out;
	m_lastError.clear();

	std::wstring url = CombineUrl(m_baseUrl, L"news.json");
	std::wstring tempDir = JoinPath(GetModuleDir(), L"_temp");
	MakeDirs(tempDir);
	std::wstring tempFile = JoinPath(tempDir, L"news.json");

	HttpClient http;
	http.SetUserAgent(L"PWLauncher/News");
	DWORD status = 0;
	if (!http.Download(url, tempFile, nullptr, 0, &status))
	{
		m_lastError = Format(L"news.json indisponivel (HTTP %u)", status);
		if (m_log) m_log(L"[News] " + m_lastError);
		DeleteFileW(tempFile.c_str());
		return out;
	}

	std::string text;
	ReadFileUtf8(tempFile, text);
	DeleteFileW(tempFile.c_str());

	json::Value root; std::string jerr;
	if (!json::Parse(text, root, jerr))
	{
		m_lastError = L"news.json invalido: " + Widen(jerr, CP_ACP);
		if (m_log) m_log(L"[News] " + m_lastError);
		return out;
	}

	const json::Value* items = root.Find("items");
	if (!items || !items->IsArray()) return out;

	for (const json::Value& it : items->arr)
	{
		if ((int)out.size() >= count) break;
		if (it.GetInt("active", 0) == 0) continue;
		NewsItem n;
		n.id = Widen(it.GetString("id"), CP_UTF8);
		n.active = true;
		if (const json::Value* v = it.Find("title"))   n.title = ParseTranslated(*v);
		if (const json::Value* v = it.Find("summary")) n.summary = ParseTranslated(*v);
		if (const json::Value* v = it.Find("link"))    n.link = ParseTranslated(*v);
		n.imageUrl = Widen(it.GetString("image"), CP_UTF8);
		n.dateIso = Widen(it.GetString("date"), CP_UTF8);
		out.push_back(std::move(n));
	}
	return out;
}

}
