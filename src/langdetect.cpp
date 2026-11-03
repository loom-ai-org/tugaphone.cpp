#include "tugaphone/langdetect.hpp"

#include <cmath>
#include <filesystem>
#include <mutex>

#include "langdetect_internal.hpp"
#include "markov.hpp"
#include "unicode_norm.hpp"

namespace tugaphone::langdetect {

using detail::U32;
using detail::U32View;

namespace {

// PORTUGUESE_KEEP, as the Python source lists it (split on whitespace).
constexpr const char* kKeepText = R"KEEP(

o a os as um uma uns umas de do da dos das em no na nos nas por pra pro
para com sem sob sobre entre ate até desde após trás e ou nem mas porém
que se como quando onde quem qual quais cujo cuja quanto porque porquê
não sim já ainda também só apenas muito pouco mais menos bem mal assim
tão tanto cada todo toda todos todas algum alguma nenhum nenhuma outro outra
este esta estes estas esse essa esses essas aquele aquela aqueles aquelas
isto isso aquilo meu minha teu tua seu sua nosso nossa vosso vossa
eu tu ele ela nós vós eles elas me te lhe nos vos lhes mim ti si
é foi era são somos sou está estão estava estou tem têm tinha há havia
ser estar ter haver fazer ir vir dar ver dizer poder querer saber
aqui ali aí lá cá agora hoje ontem amanhã sempre nunca depois antes então
olá oi bom boa obrigado obrigada por favor tchau adeus
és sois éramos fui fomos seremos fosses fôssemos fôsseis for fores fordes
vais vai vamos ides íeis irás irá iremos iríamos ide tens tendes tive
tiveste tivestes teremos tiver tiveres tiverdes estás estamos estarás
estaremos estaria estarias estaríamos estiveres hás havemos havíamos houve
houveste haverei haveremos houvesse houver houveres faço farás faremos
faríamos faça fizer vejo vedes víamos vi viste vistes verás veremos vejas
visse vires virdes vens vindes vieste viemos virei virás virá viremos viria
virias viesse viessem vier vieres viermos vierem dais dávamos deste demos
destes darei daremos daria deem desse der deres derem podíamos pude pudemos
possa pudesses pudéssemos puder puderes puderem quero queres quer queremos
querias quis quiseste quererá quereremos quereria quisesse quiser quiseres
quiserem sei sabes sabe sabias sabíamos soube soubeste saberemos saiba
saibas saibamos saibam soubesse soubessem souber souberes
eras éreis eram foste fostes foram serei serás será sereis serão fosse
fossem formos forem vou vão ia ias íamos iam irei ireis irão iria irias
iríeis iriam vá vás vades tenho temos teve tivemos tiveram terei terás terá
tereis terão tivermos tiverem estais estarei estará estareis estarão
estaríeis estariam estiver estivermos estiverdes estiverem hei heis hão
havias havieis haviam houvemos houvestes houveram haverás haverá havereis
haverão houvesses houvéssemos houvessem houvermos houverdes houverem fazes
faz fazemos fazeis fazem farei fará fareis farão faria farias faríeis fariam
faças façamos façais façam fizeres fizermos fizerdes fizerem vês vê vemos
veem via vias víeis viam viu vimos viram verei verá vereis verão veja
vejamos vejais vejam visses víssemos vissem virmos virem venho vem vêm vim
veio viestes vieram vireis virão viríamos viriam viesses viéssemos vierdes
dou dás dá damos dão dava davas davam dei deu deram darás dará dareis darão
darias daríamos dariam dê dês dêmos desses déssemos dessem dermos derdes
podia podias podiam pudeste pôde pudestes puderam possas possamos possais
possam pudesse pudessem pudermos puderdes quereis querem queria queríamos
queriam quisemos quisestes quiseram quererei quererás quererão quereríamos
quereriam quisesses quiséssemos quisessem quisermos quiserdes sabemos sabeis
sabem sabia sabiam soubemos soubestes souberam saberei saberás saberá
sabereis saberão saibais soubesses soubéssemos soubermos souberdes souberem
mãe mães pai pais pão pães mão mãos irmã irmãs irmão irmãos avó avô avós
você vocês coração)KEEP";

constexpr char32_t kCedilla = 0x0327;

const std::set<std::string>& keep_folded() {
    static const std::set<std::string> folded = [] {
        std::set<std::string> out;
        for (const auto& w : portuguese_keep()) out.insert(fold_diacritics(w));
        return out;
    }();
    return folded;
}

}  // namespace

namespace internal {

U32 normalize(U32View word) {
    const U32 lowered = detail::lower(detail::nfc(word));
    U32 out;
    for (char32_t c : lowered) {
        if (detail::is_alpha(c)) out.push_back(c);
    }
    return out;
}

std::vector<U32> wrap(U32View word) {
    std::vector<U32> out;
    out.reserve(word.size() + 2);
    out.emplace_back(1, U'\x02');
    for (char32_t c : word) out.emplace_back(1, c);
    out.emplace_back(1, U'\x03');
    return out;
}

}  // namespace internal

const std::vector<std::string>& langs() {
    static const std::vector<std::string> value{"pt", "es", "fr", "en"};
    return value;
}

const std::set<std::string>& portuguese_keep() {
    static const std::set<std::string> value = [] {
        std::set<std::string> out;
        for (const auto& w : detail::split_ws(detail::u(kKeepText))) out.insert(detail::s8(w));
        return out;
    }();
    return value;
}

std::string fold_diacritics(std::string_view word) {
    const U32 decomposed = detail::nfd(detail::u(word));
    U32 kept;
    for (char32_t c : decomposed) {
        if (detail::combining(c) == 0 || c == kCedilla) kept.push_back(c);
    }
    return detail::s8(detail::nfc(kept));
}

bool is_keep_word(std::string_view word) {
    const std::string core = detail::s8(internal::normalize(detail::u(word)));
    return !core.empty() && (portuguese_keep().count(core) || keep_folded().count(fold_diacritics(core)));
}

MarkovLangDetector::MarkovLangDetector(std::vector<Model> models) : models_(std::move(models)) {}

std::optional<MarkovLangDetector> MarkovLangDetector::from_dir(const std::string& model_dir) {
    namespace fs = std::filesystem;
    const fs::path dir = model_dir.empty() ? fs::path(detail::data_path("langdetect")) : fs::path(model_dir);
    std::vector<Model> models;
    for (const auto& lang : langs()) {
        const fs::path gz = dir / (lang + ".json.gz");
        const fs::path plain = dir / (lang + ".json");
        try {
            std::error_code ec;
            if (fs::is_regular_file(gz, ec)) {
                models.push_back({lang, std::make_shared<markov::MarkovChain>(markov::MarkovChain::from_json(
                                            markov::MarkovChain::read_gzip(gz.string())))});
            } else if (fs::is_regular_file(plain, ec)) {
                models.push_back({lang, std::make_shared<markov::MarkovChain>(
                                            markov::MarkovChain::from_json(detail::read_file(plain.string())))});
            } else {
                return std::nullopt;
            }
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return MarkovLangDetector(std::move(models));
}

Scores MarkovLangDetector::score(std::string_view word) const {
    const std::vector<std::vector<U32>> seq{internal::wrap(internal::normalize(detail::u(word)))};
    Scores out;
    for (const auto& m : models_) {
        const double ppx = m.chain->perplexity(seq);
        out.emplace_back(m.lang, std::log(1e-30 > ppx ? 1e-30 : ppx));
    }
    return out;
}

std::pair<std::string, Scores> MarkovLangDetector::detect(std::string_view word, double margin,
                                                          const std::optional<std::string>& default_side) const {
    const U32 core = internal::normalize(detail::u(word));
    if (core.empty()) return {"pt", {}};
    if (is_keep_word(detail::s8(core))) return {"pt", {}};
    const Scores scores = score(word);
    auto get = [&](const std::string& lang) -> const double* {
        for (const auto& [k, v] : scores) {
            if (k == lang) return &v;
        }
        return nullptr;
    };
    // min(scores, key=scores.get): the first key whose score no later key beats.
    size_t best = 0;
    for (size_t i = 1; i < scores.size(); ++i) {
        if (scores[i].second < scores[best].second) best = i;
    }
    const std::string& best_lang = scores.at(best).first;
    const double best_score = scores[best].second;
    if (best_lang == "pt") return {"pt", scores};
    const double* pt = get("pt");
    if (pt == nullptr) throw KeyError("'pt'");
    if ((*pt - best_score) < margin) return {"pt", scores};
    if (default_side) {
        if (const double* side = get(*default_side)) {
            if ((*side - best_score) < margin) return {*default_side, scores};
        }
    }
    return {best_lang, scores};
}

bool MarkovLangDetector::is_contact(std::string_view word, double margin) const {
    return detect(word, margin).first != "pt";
}

const MarkovLangDetector* get_detector() {
    static const std::optional<MarkovLangDetector> detector = MarkovLangDetector::from_dir();
    return detector ? &*detector : nullptr;
}

}  // namespace tugaphone::langdetect
