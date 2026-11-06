// The records call the deprecated lexicon_region on purpose.
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

#include "tugaphone/registry.hpp"

#include <iostream>
#include <sstream>

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace rg = tugaphone::registry;

std::string record_registry(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "list_dialects") return "[" + J(fn) + "," + J(rg::list_dialects()) + "]";
    if (fn == "PACKAGED_LEXICON_LECTS") {
        const auto& lects = rg::PACKAGED_LEXICON_LECTS();
        return "[" + J(fn) + "," + J(std::vector<std::string>(lects.begin(), lects.end())) + "]";
    }
    if (fn == "LEXICON_REGION_REMOVAL_VERSION") return "[" + J(fn) + "," + J(rg::LEXICON_REGION_REMOVAL_VERSION) + "]";
    if (fn == "default_filter") {
        std::vector<std::string> codes;
        for (const Value& code : v[1].items) codes.push_back(code.str);
        std::ostringstream captured;
        std::streambuf* const stderr_buffer = std::cerr.rdbuf(captured.rdbuf());
        for (const std::string& code : codes) rg::resolve_lect(code);
        std::cerr.rdbuf(stderr_buffer);
        std::vector<std::string> texts;
        std::istringstream lines(captured.str());
        const std::string category = "UserWarning: ";
        for (std::string line; std::getline(lines, line);) {
            texts.push_back(line.rfind(category, 0) == 0 ? line.substr(category.size()) : line);
        }
        return "[" + J(fn) + "," + J(codes) + "," + J(texts) + "]";
    }
    if (fn == "code") {
        const std::string code = v[1].str;
        return "[" + J(fn) + "," + J(code) + "," + guard([&] { return J(rg::normalize_dialect_code(code)); }) + "," +
               guard([&] {
                   std::vector<std::string> warned;
                   rg::set_warning_handler([&](const std::string& message) { warned.push_back(message); });
                   const std::string lect = rg::resolve_lect(code);
                   rg::set_warning_handler(nullptr);
                   return list({J(lect), J(warned)});
               }) + "," +
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
