// Four threads build the first tokenizer Sentence of a dialect code while four
// threads make the first codeswitch contact transcriptions, so the tokenizer's
// spec lookups and the contact engines load orthography2ipa specs at the same
// time. Under -fsanitize=thread a data race fails the test. In every build each
// result must equal the result of one thread afterwards.
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "tugaphone/codeswitch.hpp"
#include "tugaphone/dialects.hpp"
#include "tugaphone/tokenizer.hpp"

namespace {

std::string sentence_ipa(const std::string& code, const std::string& text) {
    auto dialect = std::make_shared<tugaphone::dialects::EuropeanPortuguese>(code);
    return tugaphone::tokenizer::Sentence(text, dialect).ipa();
}

}  // namespace

int main() {
    const std::string text = "que rapidamente o rapaz falou do Uruguai";
    std::vector<std::function<std::string()>> calls = {
        [&] { return sentence_ipa("pt-PT-x-porto", text); },
        [&] { return sentence_ipa("pt-PT-x-acores", text); },
        [&] { return sentence_ipa("pt-CV", text); },
        [&] { return sentence_ipa("pt-ST", text); },
        [] { return tugaphone::codeswitch::transcribe_contact("software", "en"); },
        [] { return tugaphone::codeswitch::transcribe_contact("fiesta", "es"); },
        [] { return tugaphone::codeswitch::transcribe_contact("croissant", "fr"); },
        [] { return tugaphone::codeswitch::transcribe_contact("weekend", "en"); },
    };
    std::vector<std::string> threaded(calls.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < calls.size(); ++i) {
        threads.emplace_back([&, i] { threaded[i] = calls[i](); });
    }
    for (auto& t : threads) t.join();
    int failures = 0;
    for (size_t i = 0; i < calls.size(); ++i) {
        const std::string serial = calls[i]();
        if (threaded[i] != serial || serial.empty()) {
            std::cerr << "call " << i << ": thread gave \"" << threaded[i] << "\", serial gave \"" << serial << "\"\n";
            ++failures;
        }
    }
    std::cout << calls.size() << " threads, " << failures << " failures\n";
    return failures ? 1 : 0;
}
