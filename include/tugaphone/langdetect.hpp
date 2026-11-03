// tugaphone.langdetect: word-level language detection for code-switch
// routing. Four character-level Markov models (pt, es, fr, en) score a word;
// a foreign model must beat Portuguese by a margin to route the word out.
#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tugaphone::markov {
class MarkovChain;
}

namespace tugaphone::langdetect {

// LANGS: the languages the detector scores, Portuguese first.
const std::vector<std::string>& langs();
// DEFAULT_MARGIN, in nats per character.
inline constexpr double DEFAULT_MARGIN = 0.5;
// PORTUGUESE_KEEP: Portuguese wordforms that are never routed out.
const std::set<std::string>& portuguese_keep();

// Strip every combining mark of `word` but the cedilla, then recompose (NFC).
std::string fold_diacritics(std::string_view word);
// Whether `word` is on PORTUGUESE_KEEP, ignoring lost diacritics.
bool is_keep_word(std::string_view word);

// A {language: score} dict, in insertion order.
using Scores = std::vector<std::pair<std::string, double>>;

class MarkovLangDetector {
public:
    struct Model {
        std::string lang;
        std::shared_ptr<const markov::MarkovChain> chain;
    };
    explicit MarkovLangDetector(std::vector<Model> models);

    // The four <lang>.json.gz (or <lang>.json) models of `model_dir`, or
    // nullopt when one is missing or does not load. The default directory is
    // <data dir>/langdetect.
    static std::optional<MarkovLangDetector> from_dir(const std::string& model_dir = "");

    // Length-normalized log-perplexity per language; lower is a better fit.
    Scores score(std::string_view word) const;
    // (language, scores) with Portuguese as the default. `default_side`
    // breaks a tie between foreign models that sit within `margin`.
    std::pair<std::string, Scores> detect(std::string_view word, double margin = DEFAULT_MARGIN,
                                          const std::optional<std::string>& default_side = std::nullopt) const;
    // Whether `word` is contact-language material.
    bool is_contact(std::string_view word, double margin = DEFAULT_MARGIN) const;

private:
    std::vector<Model> models_;
};

// The cached detector over the bundled models, or nullptr when unavailable.
const MarkovLangDetector* get_detector();

}  // namespace tugaphone::langdetect
