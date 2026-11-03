// Examples from the docstrings and tests of the Python package, checked
// against the C++ API.
#include <iostream>
#include <string>

#include "tugaphone/registry.hpp"

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}
}  // namespace

int main() {
    namespace rg = tugaphone::registry;
    check(rg::resolve_lect("pt-PT-x-porto") == "pt-PT-x-porto", "resolve_lect pt-PT-x-porto");
    check(rg::normalize_dialect_code("PT-pt-X-PORTO") == "pt-PT-x-porto", "normalize_dialect_code");
    check(rg::resolve_lect("pt-pt-x-azores") == "pt-PT-x-acores", "alias pt-pt-x-azores");
    check(rg::resolve_lect("pt-PT-x-unknown") == "pt-PT", "pop trailing subtags");
    std::cout << (failures ? "examples: FAIL\n" : "examples: ok\n");
    return failures ? 1 : 0;
}
