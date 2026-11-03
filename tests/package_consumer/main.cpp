// Built against an installed tugaphone to prove the CMake package and the
// installed data directories work.
#include <tugaphone/dialects.hpp>
#include <tugaphone/registry.hpp>

int main() {
    const bool ok = tugaphone::registry::resolve_lect("pt-pt-x-azores") == "pt-PT-x-acores" &&
                    tugaphone::dialects::EuropeanPortuguese().IRREGULAR_WORDS.size() > 1000;
    return ok ? 0 : 1;
}
