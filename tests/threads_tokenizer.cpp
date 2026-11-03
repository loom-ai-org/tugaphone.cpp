// Eight threads each build the first Sentence of a dialect code no other
// thread uses, so the first spec lookups of the tokenizer run at the same time.
// Under -fsanitize=thread a data race fails the test. In every build the IPA of
// each thread must equal the IPA that one thread gives afterwards.
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tugaphone/dialects.hpp"
#include "tugaphone/tokenizer.hpp"

namespace {

std::string ipa(const std::string& code, const std::string& text) {
    auto dialect = std::make_shared<tugaphone::dialects::EuropeanPortuguese>(code);
    return tugaphone::tokenizer::Sentence(text, dialect).ipa();
}

}  // namespace

int main() {
    const std::vector<std::string> codes = {"pt-PT-x-porto", "pt-PT-x-braga",   "pt-PT-x-acores", "pt-PT-x-madeira",
                                            "pt-PT-x-algarve", "pt-CV", "pt-GW", "pt-ST"};
    const std::string text = "que rapidamente o rapaz falou do Uruguai";
    std::vector<std::string> threaded(codes.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < codes.size(); ++i) {
        threads.emplace_back([&, i] { threaded[i] = ipa(codes[i], text); });
    }
    for (auto& t : threads) t.join();
    int failures = 0;
    for (size_t i = 0; i < codes.size(); ++i) {
        const std::string serial = ipa(codes[i], text);
        if (threaded[i] != serial || serial.empty()) {
            std::cerr << codes[i] << ": thread gave \"" << threaded[i] << "\", serial gave \"" << serial << "\"\n";
            ++failures;
        }
    }
    std::cout << codes.size() << " threads, " << failures << " failures\n";
    return failures ? 1 : 0;
}
