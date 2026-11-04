#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace el {
namespace json {

struct Value
{
	enum Type { Null, Bool, Number, String, Array, Object } type = Null;

	bool b = false;
	double num = 0;
	std::string str;
	std::vector<Value> arr;
	std::map<std::string, Value> obj;

	bool IsNull() const { return type == Null; }
	bool IsObject() const { return type == Object; }
	bool IsArray() const { return type == Array; }
	bool IsString() const { return type == String; }
	bool IsNumber() const { return type == Number; }

	const Value* Find(const std::string& key) const
	{
		if (type != Object) return nullptr;
		auto it = obj.find(key);
		return it == obj.end() ? nullptr : &it->second;
	}

	std::string GetString(const std::string& key, const std::string& def = "") const
	{
		const Value* v = Find(key);
		return (v && v->IsString()) ? v->str : def;
	}
	double GetNumber(const std::string& key, double def = 0) const
	{
		const Value* v = Find(key);
		return (v && v->IsNumber()) ? v->num : def;
	}
	int64_t GetInt(const std::string& key, int64_t def = 0) const
	{
		const Value* v = Find(key);
		return (v && v->IsNumber()) ? (int64_t)v->num : def;
	}
};

bool Parse(const std::string& text, Value& out, std::string& err);

Value MakeObject();
Value MakeArray();
Value MakeString(const std::string& s);
Value MakeNumber(double n);
Value MakeBool(bool b);

void Set(Value& object, const std::string& key, const Value& v);
void Set(Value& object, const std::string& key, const std::string& s);
void Set(Value& object, const std::string& key, const char* s);
void Set(Value& object, const std::string& key, double n);
void Set(Value& object, const std::string& key, int n);
void Set(Value& object, const std::string& key, int64_t n);
void Set(Value& object, const std::string& key, bool b);

std::string Write(const Value& v, bool pretty = false);

}
}
