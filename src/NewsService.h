#pragma once
#include <string>
#include <vector>
#include <functional>

namespace el {

struct TranslatedString
{
	std::wstring ru, en, uk, pt, es;

	std::wstring Get(const std::wstring& lang) const
	{
		std::wstring l = lang;
		for (wchar_t& c : l) c = (wchar_t)towlower(c);
		auto pick = [](const std::wstring& a, const std::wstring& b) { return a.empty() ? b : a; };
		if (l == L"ru") return pick(ru, pick(en, ru));
		if (l == L"uk") return pick(uk, pick(en, pick(ru, uk)));
		if (l == L"pt") return pick(pt, pick(en, pick(ru, pt)));
		if (l == L"es") return pick(es, pick(en, pick(ru, es)));
		return pick(en, pick(ru, pick(uk, pick(pt, es))));
	}
};

struct NewsItem
{
	std::wstring id;
	bool active = false;
	TranslatedString title;
	TranslatedString summary;
	TranslatedString link;
	std::wstring imageUrl;
	std::wstring dateIso;
};

// Le <base>news.json (mesmo formato do launcher de referencia):
// { "items": [ { id, active, title{en,ru,uk,pt,es}, summary{...}, link{...}, image, date } ] }
class NewsService
{
public:
	NewsService(const std::wstring& baseUrl, std::function<void(const std::wstring&)> log);

	std::vector<NewsItem> LoadTop(int count);
	const std::wstring& LastError() const { return m_lastError; }

private:
	std::wstring m_baseUrl;
	std::function<void(const std::wstring&)> m_log;
	std::wstring m_lastError;
};

}
