// Built against an installed tugaphone to prove the CMake package and the
// installed data directories work.
#include <tugaphone/dialects.hpp>
#include <tugaphone/plugin.hpp>
#include <tugaphone/registry.hpp>
#include <tugaphone/tugaphone.hpp>

#include <string>
#include <vector>

int main() {
    const tugaphone::TugaPhonemizer phonemizer;
    const bool ok = tugaphone::registry::resolve_lect("pt-pt-x-azores") == "pt-PT-x-acores" &&
                    tugaphone::dialects::EuropeanPortuguese().IRREGULAR_WORDS.size() > 1000 &&
                    phonemizer.phonemize_sentence("o gato") == "o ˈɡatu" &&
                    tugaphone::plugin::SilabificadorSyllabifier().syllabify("gato") ==
                        std::vector<std::string>{"ga", "to"};
    return ok ? 0 : 1;
}
