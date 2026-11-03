#include <map>
#include <memory>

#include "common.hpp"
#include "detail.hpp"
#include "markov.hpp"
#include "modules.hpp"

namespace parity {

namespace mk = tugaphone::markov;
namespace td = tugaphone::detail;

namespace {

std::map<std::string, std::shared_ptr<mk::MarkovChain>>& models() {
    static std::map<std::string, std::shared_ptr<mk::MarkovChain>> m;
    return m;
}

std::vector<td::U32> tokens(const Value& v) {
    std::vector<td::U32> out;
    for (const auto& item : v.items) out.push_back(td::u(item.str));
    return out;
}

}  // namespace

std::string record_markov(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn == "model") {
        const std::string& name = v[1].str;
        const std::string result = guard([&] {
            const std::string text = v[2].is_null()
                                         ? mk::MarkovChain::read_gzip(td::data_path("langdetect/" + name + ".json.gz"))
                                         : v[2].str;
            auto mc = std::make_shared<mk::MarkovChain>(mk::MarkovChain::from_json(text));
            models()[name] = mc;
            return list({J(static_cast<long long>(mc->order())), J(static_cast<unsigned long>(mc->vocab_size())),
                         J(mc->kneser_ney()), J(static_cast<unsigned long>(mc->context_count()))});
        });
        return head(v) + result + "]";
    }
    const auto& mc = *models().at(v[1].str);
    if (fn == "perplexity") {
        std::vector<std::vector<td::U32>> seqs;
        for (const auto& s : v[2].items) seqs.push_back(tokens(s));
        return head(v) + guard([&] { return F(mc.perplexity(seqs)); }) + "]";
    }
    if (fn == "prob") {
        return head(v) + guard([&] { return F(mc.prob(tokens(v[2]), v[3].integer)); }) + "]";
    }
    return "unknown call " + fn;
}

}  // namespace parity
