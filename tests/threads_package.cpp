// Ten threads each make the first call of a package entry point: the
// TugaPhonemizer methods, the lattice pipeline and the command line, from cold
// caches. Under -fsanitize=thread a data race fails the test. In every build
// each result must equal the result of the same call made afterwards from
// one thread.
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <orthography2ipa/orthography2ipa.hpp>

#include "cli.hpp"
#include "tugaphone/lattice_core.hpp"
#include "tugaphone/tugaphone.hpp"

namespace {

const char* const kText = "Tenho 23 anos, Sr. Silva; li o Cap. XII. Fiz o download do software.";

std::string cli(const std::vector<std::string>& args) {
    std::ostringstream out, err;
    const int status = tugaphone::cli::main(args, out, err);
    return std::to_string(status) + "|" + out.str() + "|" + err.str();
}

}  // namespace

int main() {
    using tugaphone::TugaPhonemizer;
    const std::vector<std::function<std::string()>> calls = {
        [] { return TugaPhonemizer().phonemize_sentence(kText, "pt-BR", "auto"); },
        [] { return TugaPhonemizer().phonemize_sentence(kText, "pt-AO", "en"); },
        [] { return TugaPhonemizer().phonemize_sentence(kText, "pt-PT", "es"); },
        [] { return TugaPhonemizer().phonemize_sentence("jogo", "pt-MZ"); },
        [] { return TugaPhonemizer::force_accent("o vinho verde", "pt-PT-x-porto", "respell", "pt-BR"); },
        [] { return TugaPhonemizer::force_accent("a tia", "pt-BR", "respell", "pt-PT"); },
        [] { return tugaphone::lattice_core::pipeline("pt-BR").expand(" Tenho 23 anos. "); },
        [] { return std::string(TugaPhonemizer::is_supported("pt-pt-x-azores") ? "true" : "false"); },
        [] { return cli({"phonemize", kText, "--lect", "pt-PT-x-porto"}); },
        [] { return cli({"list"}); },
    };
    std::vector<std::string> threaded(calls.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < calls.size(); ++i) threads.emplace_back([&, i] { threaded[i] = calls[i](); });
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
