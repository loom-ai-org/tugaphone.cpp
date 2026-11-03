// The part of markovonnx 1.0.2a1 that tugaphone uses: MarkovChain.load of a
// JSON model, and MarkovChain.perplexity with the probabilities of
// _get_probs. Every float operation repeats numpy's: the count rows are
// float32, the Kneser-Ney discounts are float64, numpy's pairwise summation
// adds the discount mass, and numpy's scalar promotion keeps the
// interpolation term in float32.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "detail.hpp"

namespace tugaphone::markov {

using detail::U32;

// numpy's pairwise summation of a contiguous array (np.sum, np.add.reduce).
double pairwise_sum(const double* a, size_t n);
float pairwise_sum(const float* a, size_t n);

class MarkovChain {
public:
    // MarkovChain.load(path) of the JSON text; the exception that Python's
    // json.load or MarkovChain.load raises otherwise.
    static MarkovChain from_json(std::string_view utf8);
    // The JSON text of a gzip file, as gzip.open(path).read() gives it.
    static std::string read_gzip(const std::string& path);

    long long order() const { return order_; }
    // Vocabulary.size: the number of symbols, <UNK> included.
    size_t vocab_size() const { return id2tok_.size(); }
    const std::vector<U32>& id2tok() const { return id2tok_; }
    bool kneser_ney() const { return kneser_ney_; }
    // len(self._counts): the number of distinct contexts.
    size_t context_count() const { return counts_.size(); }

    // Vocabulary.encode and Vocabulary.decode.
    std::vector<long long> encode(const std::vector<U32>& tokens) const;
    std::vector<U32> decode(const std::vector<long long>& ids) const;

    // float(self._get_probs(context)[next_id]).
    double prob(const std::vector<U32>& context, long long next_id) const;
    // MarkovChain.perplexity(sequences).
    double perplexity(const std::vector<std::vector<U32>>& sequences) const;

private:
    struct Row {
        std::vector<float> counts;
        float total = 0;      // row.sum(), in float32
        float kn_term = 0;    // discount_mass * (1.0 / V), in float32
    };

    long long ctx_index(const std::vector<long long>& ids) const;
    double row_prob(const Row& row, long long next_id) const;

    long long order_ = 0;
    double smoothing_ = 1e-5;
    bool kneser_ney_ = false;
    double kn_d1_ = 0.75, kn_d2_ = 0.75, kn_d3_ = 0.75;
    std::vector<U32> id2tok_;
    std::unordered_map<U32, long long> tok2id_;
    std::unordered_map<long long, Row> counts_;
};

}  // namespace tugaphone::markov
