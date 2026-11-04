#include "Json.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace el {
namespace json {

namespace {

struct Parser
{
	const char* p;
	const char* end;
	std::string err;

	void SkipWs()
	{
		while (p < end && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) ++p;
	}

	bool Fail(const char* m) { err = m; return false; }

	bool ParseValue(Value& v)
	{
		SkipWs();
		if (p >= end) return Fail("fim inesperado");
		char c = *p;
		if (c == '{') return ParseObject(v);
		if (c == '[') return ParseArray(v);
		if (c == '"') { v.type = Value::String; return ParseString(v.str); }
		if (c == 't') { if (end - p >= 4 && !strncmp(p, "true", 4)) { p += 4; v.type = Value::Bool; v.b = true; return true; } return Fail("bool"); }
		if (c == 'f') { if (end - p >= 5 && !strncmp(p, "false", 5)) { p += 5; v.type = Value::Bool; v.b = false; return true; } return Fail("bool"); }
		if (c == 'n') { if (end - p >= 4 && !strncmp(p, "null", 4)) { p += 4; v.type = Value::Null; return true; } return Fail("null"); }
		return ParseNumber(v);
	}

	bool ParseNumber(Value& v)
	{
		char* stop = nullptr;
		double d = strtod(p, &stop);
		if (stop == p) return Fail("numero invalido");
		p = stop;
		v.type = Value::Number;
		v.num = d;
		return true;
	}

	bool ParseString(std::string& out)
	{
		if (p >= end || *p != '"') return Fail("string esperada");
		++p;
		out.clear();
		while (p < end)
		{
			char c = *p++;
			if (c == '"') return true;
			if (c == '\\')
			{
				if (p >= end) return Fail("escape invalido");
				char e = *p++;
				switch (e)
				{
				case '"': out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/': out.push_back('/'); break;
				case 'b': out.push_back('\b'); break;
				case 'f': out.push_back('\f'); break;
				case 'n': out.push_back('\n'); break;
				case 'r': out.push_back('\r'); break;
				case 't': out.push_back('\t'); break;
				case 'u':
				{
					if (end - p < 4) return Fail("\\u invalido");
					unsigned int cp = 0;
					for (int i = 0; i < 4; ++i)
					{
						char h = *p++;
						cp <<= 4;
						if (h >= '0' && h <= '9') cp |= (h - '0');
						else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
						else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
						else return Fail("hex invalido");
					}
					if (cp < 0x80) out.push_back((char)cp);
					else if (cp < 0x800)
					{
						out.push_back((char)(0xC0 | (cp >> 6)));
						out.push_back((char)(0x80 | (cp & 0x3F)));
					}
					else
					{
						out.push_back((char)(0xE0 | (cp >> 12)));
						out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
						out.push_back((char)(0x80 | (cp & 0x3F)));
					}
					break;
				}
				default: return Fail("escape desconhecido");
				}
			}
			else out.push_back(c);
		}
		return Fail("string sem fim");
	}

	bool ParseArray(Value& v)
	{
		v.type = Value::Array;
		++p;
		SkipWs();
		if (p < end && *p == ']') { ++p; return true; }
		while (p < end)
		{
			Value item;
			if (!ParseValue(item)) return false;
			v.arr.push_back(std::move(item));
			SkipWs();
			if (p >= end) return Fail("array sem fim");
			if (*p == ',') { ++p; continue; }
			if (*p == ']') { ++p; return true; }
			return Fail("array: , ou ] esperado");
		}
		return Fail("array sem fim");
	}

	bool ParseObject(Value& v)
	{
		v.type = Value::Object;
		++p;
		SkipWs();
		if (p < end && *p == '}') { ++p; return true; }
		while (p < end)
		{
			SkipWs();
			std::string key;
			if (!ParseString(key)) return false;
			SkipWs();
			if (p >= end || *p != ':') return Fail(": esperado");
			++p;
			Value val;
			if (!ParseValue(val)) return false;
			v.obj[key] = std::move(val);
			SkipWs();
			if (p >= end) return Fail("objeto sem fim");
			if (*p == ',') { ++p; continue; }
			if (*p == '}') { ++p; return true; }
			return Fail("objeto: , ou } esperado");
		}
		return Fail("objeto sem fim");
	}
};

}

Value MakeObject() { Value v; v.type = Value::Object; return v; }
Value MakeArray() { Value v; v.type = Value::Array; return v; }
Value MakeString(const std::string& s) { Value v; v.type = Value::String; v.str = s; return v; }
Value MakeNumber(double n) { Value v; v.type = Value::Number; v.num = n; return v; }
Value MakeBool(bool b) { Value v; v.type = Value::Bool; v.b = b; return v; }

void Set(Value& object, const std::string& key, const Value& v)
{
	if (object.type != Value::Object) object.type = Value::Object;
	object.obj[key] = v;
}
void Set(Value& object, const std::string& key, const std::string& s) { Set(object, key, MakeString(s)); }
void Set(Value& object, const std::string& key, const char* s) { Set(object, key, MakeString(s ? s : "")); }
void Set(Value& object, const std::string& key, double n) { Set(object, key, MakeNumber(n)); }
void Set(Value& object, const std::string& key, int n) { Set(object, key, MakeNumber((double)n)); }
void Set(Value& object, const std::string& key, int64_t n) { Set(object, key, MakeNumber((double)n)); }
void Set(Value& object, const std::string& key, bool b) { Set(object, key, MakeBool(b)); }

namespace {

void AppendEscaped(std::string& out, const std::string& s)
{
	out.push_back('"');
	for (unsigned char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\b': out += "\\b"; break;
		case '\f': out += "\\f"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (c < 0x20)
			{
				char buf[8];
				snprintf(buf, sizeof(buf), "\\u%04x", c);
				out += buf;
			}
			else out.push_back((char)c);
		}
	}
	out.push_back('"');
}

void AppendNumber(std::string& out, double n)
{
	char buf[64];
	if (n == (double)(int64_t)n && n < 9e15 && n > -9e15)
		snprintf(buf, sizeof(buf), "%lld", (long long)n);
	else
		snprintf(buf, sizeof(buf), "%.17g", n);
	out += buf;
}

void WriteValue(std::string& out, const Value& v, bool pretty, int depth)
{
	std::string ind, ind1;
	if (pretty) { ind.assign(depth * 2, ' '); ind1.assign((depth + 1) * 2, ' '); }

	switch (v.type)
	{
	case Value::Null: out += "null"; break;
	case Value::Bool: out += v.b ? "true" : "false"; break;
	case Value::Number: AppendNumber(out, v.num); break;
	case Value::String: AppendEscaped(out, v.str); break;
	case Value::Array:
	{
		if (v.arr.empty()) { out += "[]"; break; }
		out.push_back('[');
		for (size_t i = 0; i < v.arr.size(); ++i)
		{
			if (pretty) { out.push_back('\n'); out += ind1; }
			WriteValue(out, v.arr[i], pretty, depth + 1);
			if (i + 1 < v.arr.size()) out.push_back(',');
		}
		if (pretty) { out.push_back('\n'); out += ind; }
		out.push_back(']');
		break;
	}
	case Value::Object:
	{
		if (v.obj.empty()) { out += "{}"; break; }
		out.push_back('{');
		size_t i = 0;
		for (const auto& kv : v.obj)
		{
			if (pretty) { out.push_back('\n'); out += ind1; }
			AppendEscaped(out, kv.first);
			out.push_back(':');
			if (pretty) out.push_back(' ');
			WriteValue(out, kv.second, pretty, depth + 1);
			if (++i < v.obj.size()) out.push_back(',');
		}
		if (pretty) { out.push_back('\n'); out += ind; }
		out.push_back('}');
		break;
	}
	}
}

}

std::string Write(const Value& v, bool pretty)
{
	std::string out;
	WriteValue(out, v, pretty, 0);
	return out;
}

bool Parse(const std::string& text, Value& out, std::string& err)
{
	Parser ps;
	ps.p = text.data();
	ps.end = text.data() + text.size();

	if (ps.end - ps.p >= 3 && (unsigned char)ps.p[0] == 0xEF
		&& (unsigned char)ps.p[1] == 0xBB && (unsigned char)ps.p[2] == 0xBF)
		ps.p += 3;

	if (!ps.ParseValue(out)) { err = ps.err; return false; }
	ps.SkipWs();
	if (ps.p != ps.end) { err = "conteudo extra apos o JSON"; return false; }
	return true;
}

}
}
