// Built against an installed tugaphone to prove the CMake package and the
// installed data directories work.
#include <tugaphone/dialects.hpp>
#include <tugaphone/registry.hpp>
#include <tugaphone/tugaphone.hpp>


int main() {
    const tugaphone::TugaPhonemizer phonemizer;
    const bool ok = tugaphone::registry::resolve_lect("pt-pt-x-azores") == "pt-PT-x-acores" &&
                    tugaphone::dialects::EuropeanPortuguese().IRREGULAR_WORDS.size() > 1000 &&
                    phonemizer.phonemize_sentence("o gato") == "u ˈɡatu" &&
                    tugaphone::registry::is_supported("pt-pt-x-azores") &&
                    !tugaphone::registry::is_supported("en-US");
    return ok ? 0 : 1;
}
