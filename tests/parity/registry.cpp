#include "tugaphone/registry.hpp"

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace rg = tugaphone::registry;

std::string record_registry(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "list_dialects") return "[" + J(fn) + "," + J(rg::list_dialects()) + "]";
    if (fn == "code") {
        const std::string code = v[1].str;
        return "[" + J(fn) + "," + J(code) + "," + guard([&] { return J(rg::normalize_dialect_code(code)); }) + "," +
               guard([&] { return J(rg::resolve_lect(code)); }) + "," +
               guard([&] { return J(rg::resolve_dialect(code)); }) + "," +
               guard([&] { return J(rg::lexicon_region(code)); }) + "," +
               guard([&] { return J(rg::default_contact(code)); }) + "," + guard([&] {
                   const auto inv = rg::get_dialect_inventory(code);
                   return list({J(inv.type_name()), J(inv.dialect_code)});
               }) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
