#include "tugaphone/dialects.hpp"

#include <algorithm>
#include <cstdio>
#include <type_traits>

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace dl = tugaphone::dialects;

namespace {

const Value* member(const Value& obj, const std::string& key) {
    for (const auto& [k, v] : obj.members) {
        if (k == key) return &v;
    }
    return nullptr;
}

void from_json(std::string& out, const Value& v) { out = v.str; }
void from_json(dl::StrSet& out, const Value& v) {
    for (const auto& item : v.items) out.insert(item.str);
}
void from_json(dl::StrList& out, const Value& v) {
    for (const auto& item : v.items) out.push_back(item.str);
}
void from_json(dl::StrMap& out, const Value& v) {
    for (const auto& [k, item] : v.members) out.assign(k, item.str);
}
void from_json(dl::SetMap& out, const Value& v) {
    for (const auto& [k, item] : v.members) from_json(out.setdefault(k), item);
}

dl::DialectKwargs kwargs_of(const Value* v) {
    dl::DialectKwargs kwargs;
    if (v == nullptr) return kwargs;
#define PARITY_KWARG(type, name)                    \
    if (const Value* item = member(*v, #name)) {    \
        kwargs.name.emplace();                      \
        from_json(*kwargs.name, *item);             \
    }
    TUGAPHONE_DIALECT_FIELDS(PARITY_KWARG)
#undef PARITY_KWARG
    return kwargs;
}

dl::DialectInventory build(const Value& spec) {
    const std::string cls = member(spec, "class")->str;
    std::optional<std::string> code;
    if (const Value* v = member(spec, "dialect_code"); v && !v->is_null()) code = v->str;
    std::optional<dl::StrMap> irregular;
    if (const Value* v = member(spec, "IRREGULAR_WORDS"); v && !v->is_null()) {
        irregular.emplace();
        from_json(*irregular, *v);
    }
    const dl::DialectKwargs kwargs = kwargs_of(member(spec, "kwargs"));
    if (cls == "DialectInventory") return dl::DialectInventory(kwargs);
    if (cls == "EuropeanPortuguese") return dl::EuropeanPortuguese(code, irregular, kwargs);
    if (cls == "LisbonPortuguese") return dl::LisbonPortuguese();
    if (cls == "BrazilianPortuguese") return dl::BrazilianPortuguese(code, irregular, kwargs);
    if (cls == "RioJaneiroPortuguese") return dl::RioJaneiroPortuguese();
    if (cls == "SaoPauloPortuguese") return dl::SaoPauloPortuguese();
    if (cls == "AngolanPortuguese") return dl::AngolanPortuguese(code, irregular, kwargs);
    if (cls == "MozambicanPortuguese") return dl::MozambicanPortuguese(code, irregular, kwargs);
    if (cls == "TimoresePortuguese") return dl::TimoresePortuguese(code, irregular, kwargs);
    throw std::runtime_error("unknown class " + cls);
}

std::string fnv1a64(const dl::StrMap& m) {
    unsigned long long h = 0xcbf29ce484222325ULL;
    for (const auto& [k, v] : m) {
        for (unsigned char b : k + "\t" + v + "\n") h = (h ^ b) * 0x100000001b3ULL;
    }
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", h);
    return buf;
}

std::string field(const std::string&, const std::string& v) { return J(v); }
std::string field(const std::string&, const dl::StrSet& v) { return J(v); }
std::string field(const std::string&, const dl::StrList& v) { return J(v); }
std::string field(const std::string& name, const dl::StrMap& m) {
    if (name == "IRREGULAR_WORDS") return list({J(static_cast<unsigned long>(m.size())), J(fnv1a64(m))});
    std::vector<std::pair<std::string, std::string>> items(m.begin(), m.end());
    if (name == "VOWEL_HEIGHT") std::sort(items.begin(), items.end());
    std::vector<std::string> parts;
    for (const auto& [k, v] : items) parts.push_back(list({J(k), J(v)}));
    return list(parts);
}
std::string field(const std::string&, const dl::SetMap& m) {
    std::vector<std::string> parts;
    for (const auto& [k, v] : m) parts.push_back(list({J(k), J(v)}));
    return list(parts);
}

std::string encode(const dl::DialectInventory& inv) {
    std::vector<std::string> fields;
#define PARITY_FIELD(type, name) fields.push_back(list({J(#name), field(#name, inv.name)}));
    TUGAPHONE_DIALECT_FIELDS(PARITY_FIELD)
#undef PARITY_FIELD
    return list({J(inv.type_name()), list(fields), J(inv.ARCHAIC_WORDS)});
}

}  // namespace

std::string record_dialects(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "inventory") {
        return "[" + J(fn) + "," + json_line::dump(v[1]) + "," + guard([&] { return encode(build(v[1])); }) + "]";
    }
    if (fn == "AO1990") return "[" + J(fn) + "," + encode(dl::AO1990()) + "]";
    if (fn == "eq") {
        return "[" + J(fn) + "," + json_line::dump(v[1]) + "," + json_line::dump(v[2]) + "," +
               guard([&] { return J(build(v[1]) == build(v[2])); }) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
