// Four threads make the first codeswitch contact transcriptions while four
// threads make the first resolve_lect calls. Both build the orthography2ipa
// resolution catalog on first use: the contact engines through get, the
// registry through resolve and resolves_exactly. The threads start together
// after a latch. Under -fsanitize=thread a data race fails the test. In every
// build each result must equal the result of one thread afterwards, and each
// resolve_lect result must equal the Python output of the registry goldens.
#include <atomic>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "tugaphone/codeswitch.hpp"
#include "tugaphone/registry.hpp"

namespace {

struct Call {
    std::function<std::string()> run;
    std::string python;
};

}  // namespace

int main() {
    const std::vector<Call> calls = {
        {[] { return tugaphone::codeswitch::transcribe_contact("software", "en"); }, ""},
        {[] { return tugaphone::codeswitch::transcribe_contact("fiesta", "es"); }, ""},
        {[] { return tugaphone::codeswitch::transcribe_contact("croissant", "fr"); }, ""},
        {[] { return tugaphone::codeswitch::transcribe_contact("weekend", "en"); }, ""},
        {[] { return tugaphone::registry::resolve_lect("roa-x-galaicopt"); }, "pt-PT"},
        {[] { return tugaphone::registry::resolve_lect("por-BR"); }, "pt-BR"},
        {[] { return tugaphone::registry::resolve_lect("glg"); }, "pt-PT"},
        {[] { return tugaphone::registry::resolve_lect("pt-BR-x-unknown"); }, "pt-BR"},
    };
    std::vector<std::string> threaded(calls.size());
    std::vector<std::thread> threads;
    std::atomic<size_t> waiting{calls.size()};
    for (size_t i = 0; i < calls.size(); ++i) {
        threads.emplace_back([&, i] {
            waiting.fetch_sub(1);
            while (waiting.load() != 0) std::this_thread::yield();
            threaded[i] = calls[i].run();
        });
    }
    for (auto& t : threads) t.join();
    int failures = 0;
    for (size_t i = 0; i < calls.size(); ++i) {
        const std::string serial = calls[i].run();
        if (threaded[i] != serial || serial.empty() || (!calls[i].python.empty() && serial != calls[i].python)) {
            std::cerr << "call " << i << ": thread gave \"" << threaded[i] << "\", serial gave \"" << serial << "\"\n";
            ++failures;
        }
    }
    std::cout << calls.size() << " threads, " << failures << " failures\n";
    return failures ? 1 : 0;
}
