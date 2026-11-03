// tugaphone.accent: bend text or IPA toward a target Lusophone accent.
//
// force_accent with mode "ipa" transcribes with the target lect. With mode
// "respell" it rewrites the spelling so that a grapheme-input voice speaking
// the base lect reads the word toward the target accent; an edit is kept only
// when the base lect's transcription of it moves closer to the target IPA. An
// AccentOverlay adds ordered, user-supplied regex or whole-word edits.
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tugaphone::accent {

// phonemize(text, lect): the transcriber respelling measures with.
using Phonemizer = std::function<std::string(const std::string& text, const std::string& lect)>;

// The transcriber that an empty Phonemizer argument stands for. By default it
// is tugaphone's lattice phonemizer (lattice_core.phonemize); until the engine
// module is linked it raises NotImplementedError. set_default_phonemizer
// replaces it, and an empty argument restores the default.
void set_default_phonemizer(Phonemizer phonemizer);

// A candidate orthographic edit toward a non-base accent. `pattern` is a
// Python regular expression over the lower-cased word. The default rules run
// as hand-written matchers; any other pattern runs on the re-compatible
// engine.
class RespellRule {
public:
    RespellRule(std::string name, std::string pattern, std::string replacement, std::string note = "");

    const std::string& name() const { return name_; }
    const std::string& pattern() const { return pattern_; }
    const std::string& replacement() const { return replacement_; }
    const std::string& note() const { return note_; }

    // [(m.start(), m.end()) for m in re.finditer(pattern, word)], in code points.
    std::vector<std::pair<size_t, size_t>> matches(std::string_view word) const;

private:
    std::string name_, pattern_, replacement_, note_;
    int builtin_;  // the hand-written matcher, or -1 for the regex engine
};

// DEFAULT_RESPELL_RULES.
const std::vector<RespellRule>& default_respell_rules();

// Respell one word so `base_lect` reads it toward `target_lect`.
std::string respell_word(std::string_view word, const std::string& target_lect, const std::string& base_lect = "pt-PT",
                         const std::vector<RespellRule>& rules = default_respell_rules(),
                         const Phonemizer& phonemize = {});
// Respell every word of `text`; punctuation and spacing pass through.
std::string respell(std::string_view text, const std::string& target_lect, const std::string& base_lect = "pt-PT",
                    const std::vector<RespellRule>& rules = default_respell_rules(), const Phonemizer& phonemize = {});

// One ordered, user-supplied edit: kind "regex" (a Python regular
// expression) or "word" (a whole word); stage "ipa" or "text".
struct Transform {
    std::string kind = "regex";
    std::string pattern;
    std::string replacement;
    std::string stage = "ipa";
    bool ignore_case = true;

    // re.sub with the pattern; ValueError for an unknown kind.
    std::string apply(std::string_view s) const;
    // json.dumps(to_dict()) with ensure_ascii=False and the default separators.
    std::string to_dict_json() const;
    // Transform.from_dict(json.loads(text)).
    static Transform from_dict_json(std::string_view text);
};

// An ordered, shareable stack of Transforms.
struct AccentOverlay {
    std::string name;
    std::vector<Transform> transforms;

    // Apply, in order, every transform of `stage`.
    std::string apply(std::string_view s, std::string_view stage) const;
    // json.dumps(to_dict()) with ensure_ascii=False and the default separators.
    std::string to_dict_json() const;
    static AccentOverlay from_dict_json(std::string_view text);
    // AccentOverlay.to_json(indent=..., ensure_ascii=...); indent nullopt is
    // Python's indent=None.
    std::string to_json(std::optional<int> indent = 2, bool ensure_ascii = false) const;
    static AccentOverlay from_json(std::string_view text);
};

// Force `text` into the `lect` accent: mode "ipa" or "respell"; ValueError
// for any other mode.
std::string force_accent(std::string_view text, std::string_view lect, std::string_view mode = "ipa",
                         std::string_view base_lect = "pt-PT", const AccentOverlay* overlay = nullptr,
                         const std::vector<RespellRule>& rules = default_respell_rules(),
                         const Phonemizer& phonemize = {});

}  // namespace tugaphone::accent
