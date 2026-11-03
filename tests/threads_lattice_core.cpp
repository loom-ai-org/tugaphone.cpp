// Eight threads each make the first phonemize call of a lect or a contact
// mode, so the engines and the contact engines are built at the same time.
// Under -fsanitize=thread a data race fails the test. In every build each
// result must equal the result of one thread afterwards, and the two
// contact sentences must give the Python output of the lattice_core goldens.
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "tugaphone/lattice_core.hpp"

namespace {

struct Call {
    std::string text, lect, contact, python;
};

}  // namespace

int main() {
    const std::vector<Call> calls = {
        {"Fiz o download do software ontem.", "pt-BR", "auto", "ˈfis u dawnlowd du saftwɛɐɾ ˈõtẽj"},
        {"O croissant estava délicieux, merci.", "pt-BR", "auto", "u kʁwasɐ̃ esˈtavɐ delisje meɾˈsi"},
        {"que rapidamente o rapaz falou", "pt-PT", "none", ""},
        {"que rapidamente o rapaz falou", "pt-AO", "none", ""},
        {"que rapidamente o rapaz falou", "pt-MZ", "none", ""},
        {"que rapidamente o rapaz falou", "pt-PT-x-porto", "none", ""},
        {"o software do Uruguai", "pt-TL", "en", ""},
        {"o software do Uruguai", "pt-CV", "es", ""},
    };
    std::vector<std::string> threaded(calls.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < calls.size(); ++i) {
        threads.emplace_back([&, i] {
            threaded[i] = tugaphone::lattice_core::phonemize(calls[i].text, calls[i].lect, calls[i].contact);
        });
    }
    for (auto& t : threads) t.join();
    int failures = 0;
    for (size_t i = 0; i < calls.size(); ++i) {
        const auto& c = calls[i];
        const std::string serial = tugaphone::lattice_core::phonemize(c.text, c.lect, c.contact);
        if (threaded[i] != serial || serial.empty() || (!c.python.empty() && serial != c.python)) {
            std::cerr << c.lect << " " << c.contact << " \"" << c.text << "\": thread gave \"" << threaded[i]
                      << "\", serial gave \"" << serial << "\"\n";
            ++failures;
        }
    }
    std::cout << calls.size() << " threads, " << failures << " failures\n";
    return failures ? 1 : 0;
}
