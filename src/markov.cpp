#include "markov.hpp"

#include <zlib.h>

#include <cmath>
#include <limits>

#include "json.hpp"

namespace tugaphone::markov {

namespace {

// numpy's @TYPE@_pairwise_sum: plain loop below 8 items, eight accumulators up
// to 128 items, and a split at a multiple of 8 above that.
template <class T>
T pairwise(const T* a, size_t n) {
    if (n < 8) {
        T res = 0;
        for (size_t i = 0; i < n; ++i) res += a[i];
        return res;
    }
    if (n <= 128) {
        T r[8];
        for (size_t k = 0; k < 8; ++k) r[k] = a[k];
        size_t i = 8;
        for (; i < n - (n % 8); i += 8) {
            for (size_t k = 0; k < 8; ++k) r[k] += a[i + k];
        }
        T res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        for (; i < n; ++i) res += a[i];
        return res;
    }
    size_t n2 = n / 2;
    n2 -= n2 % 8;
    return pairwise(a, n2) + pairwise(a + n2, n - n2);
}

// Python's bytes.decode("utf-8") in strict mode: true when `text` is
// well-formed UTF-8 with no encoded surrogate.
bool strict_utf8(std::string_view text) {
    size_t i = 0;
    const size_t n = text.size();
    auto byte = [&](size_t k) { return static_cast<unsigned char>(text[k]); };
    while (i < n) {
        const unsigned char b = byte(i);
        size_t len;
        unsigned char lo = 0x80, hi = 0xBF;
        if (b < 0x80) {
            ++i;
            continue;
        } else if (b >= 0xC2 && b <= 0xDF) {
            len = 2;
        } else if (b >= 0xE0 && b <= 0xEF) {
            len = 3;
            if (b == 0xE0) lo = 0xA0;
            if (b == 0xED) hi = 0x9F;
        } else if (b >= 0xF0 && b <= 0xF4) {
            len = 4;
            if (b == 0xF0) lo = 0x90;
            if (b == 0xF4) hi = 0x8F;
        } else {
            return false;
        }
        if (i + len > n) return false;
        if (byte(i + 1) < lo || byte(i + 1) > hi) return false;
        for (size_t k = 2; k < len; ++k) {
            if (byte(i + k) < 0x80 || byte(i + k) > 0xBF) return false;
        }
        i += len;
    }
    return true;
}

// Python truthiness of a JSON value.
bool truthy(const json::Value& v) {
    switch (v.kind) {
        case json::Value::Null: return false;
        case json::Value::Bool: return v.boolean;
        case json::Value::Int: return v.text != "0" && v.text != "-0";
        case json::Value::Float: return v.number != 0.0;
        case json::Value::String: return !v.text.empty();
        case json::Value::Array: return !v.items.empty();
        case json::Value::Object: return !v.members.empty();
    }
    return false;
}

double number(const json::Value& v, const char* what) {
    if (v.kind == json::Value::Int || v.kind == json::Value::Float) return v.number;
    if (v.kind == json::Value::Bool) return v.boolean ? 1.0 : 0.0;
    throw TypeError(std::string("markov model field is not a number: ") + what);
}

// numpy's float64 -> float32 cast: round to nearest, overflow to ±inf.
float to_f32(double x) {
    if (std::isnan(x)) return std::numeric_limits<float>::quiet_NaN();
    constexpr double kMax = static_cast<double>(std::numeric_limits<float>::max());
    // Values in (FLT_MAX, FLT_MAX + half an ulp) still round down to FLT_MAX.
    constexpr double kOverflow = kMax + 0x1p103;
    if (x >= kOverflow) return std::numeric_limits<float>::infinity();
    if (x <= -kOverflow) return -std::numeric_limits<float>::infinity();
    return static_cast<float>(x);
}

}  // namespace

double pairwise_sum(const double* a, size_t n) { return pairwise(a, n); }
float pairwise_sum(const float* a, size_t n) { return pairwise(a, n); }

std::string MarkovChain::read_gzip(const std::string& path) {
    const std::string raw = detail::read_file(path);
    std::string out;
    z_stream zs{};
    if (inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) throw Error("OSError", "zlib init failed");
    size_t pos = 0;
    char buffer[1 << 16];
    while (pos < raw.size()) {
        // Python's gzip skips the zero padding between and after members.
        if (raw[pos] == '\0') {
            ++pos;
            continue;
        }
        inflateReset(&zs);
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.data() + pos));
        zs.avail_in = static_cast<uInt>(raw.size() - pos);
        int status;
        do {
            zs.next_out = reinterpret_cast<Bytef*>(buffer);
            zs.avail_out = sizeof buffer;
            status = inflate(&zs, Z_NO_FLUSH);
            if (status != Z_OK && status != Z_STREAM_END) {
                inflateEnd(&zs);
                throw Error("BadGzipFile", "not a gzip file or a corrupt one: " + path);
            }
            out.append(buffer, sizeof buffer - zs.avail_out);
            if (status == Z_OK && zs.avail_in == 0 && zs.avail_out != 0) {
                inflateEnd(&zs);
                throw Error("EOFError", "Compressed file ended before the end-of-stream marker was reached");
            }
        } while (status != Z_STREAM_END);
        pos = raw.size() - zs.avail_in;
    }
    inflateEnd(&zs);
    return out;
}

MarkovChain MarkovChain::from_json(std::string_view utf8) {
    if (!strict_utf8(utf8)) throw Error("UnicodeDecodeError", "'utf-8' codec can't decode the model");
    if (utf8.substr(0, 3) == "\xEF\xBB\xBF") throw json::JSONDecodeError("Unexpected UTF-8 BOM");
    const json::Value data = json::loads(utf8);
    if (data.kind != json::Value::Object) throw TypeError("model is not a JSON object");

    MarkovChain mc;
    const json::Value& vocab = data.at("vocab");
    if (vocab.kind != json::Value::Object) throw TypeError("vocab is not a JSON object");
    const json::Value& id2tok = vocab.at("id2tok");
    if (id2tok.kind != json::Value::Array) throw TypeError("id2tok is not a list");
    for (const auto& tok : id2tok.items) {
        if (tok.kind != json::Value::String) throw TypeError("id2tok holds a non-string token");
        mc.tok2id_[detail::from_utf8(tok.text)] = static_cast<long long>(mc.id2tok_.size());
        mc.id2tok_.push_back(detail::from_utf8(tok.text));
    }
    // encode() looks up "<UNK>" for every unknown token: a vocabulary without
    // it fails there, so it fails here.
    if (!mc.tok2id_.count(U"<UNK>")) throw KeyError("'<UNK>'");

    const json::Value& order = data.at("order");
    if (order.kind != json::Value::Int || order.text.size() > 4 || order.number < 0)
        throw TypeError("order must be a small non-negative int");
    mc.order_ = static_cast<long long>(order.number);
    mc.smoothing_ = number(data.at("smoothing"), "smoothing");
    if (const json::Value* kn = data.find("kneser_ney")) mc.kneser_ney_ = truthy(*kn);
    if (const json::Value* d = data.find("kn_d1")) mc.kn_d1_ = number(*d, "kn_d1");
    if (const json::Value* d = data.find("kn_d2")) mc.kn_d2_ = number(*d, "kn_d2");
    if (const json::Value* d = data.find("kn_d3")) mc.kn_d3_ = number(*d, "kn_d3");

    // A context index is a base-V number of `order` digits; it must fit.
    const long double reach = std::pow(static_cast<long double>(mc.id2tok_.size()),
                                       static_cast<long double>(mc.order_));
    if (reach >= 9.0e18L) throw ValueError("context index space exceeds int64");

    const json::Value& counts = data.at("counts");
    if (counts.kind != json::Value::Object) throw TypeError("counts is not a JSON object");
    const double inv_v = 1.0 / static_cast<double>(mc.id2tok_.size());
    for (const auto& [key, values] : counts.members) {
        const auto index = py_int(key);
        if (!index) throw ValueError("invalid literal for int() with base 10: '" + key + "'");
        if (index->digits.size() > 18) throw ValueError("count key exceeds int64: " + key);
        long long ci = std::stoll(index->digits);
        if (index->negative) ci = -ci;
        if (values.kind != json::Value::Array) throw TypeError("a count row is not a list");
        Row row;
        row.counts.reserve(values.items.size());
        for (const auto& x : values.items) row.counts.push_back(to_f32(number(x, "count")));
        row.total = pairwise(row.counts.data(), row.counts.size());
        if (mc.kneser_ney_ && row.total > 0) {
            // discount_mass = float(np.sum(np.minimum(discounts, row))) / total,
            // a Python float divided by a float32 scalar, which numpy does in
            // float32; the product with the Python float 1.0 / V stays float32.
            std::vector<double> clipped(row.counts.size());
            for (size_t i = 0; i < row.counts.size(); ++i) {
                const float c = row.counts[i];
                const double d = c >= 3 ? mc.kn_d3_ : c == 2 ? mc.kn_d2_ : c == 1 ? mc.kn_d1_ : 0.0;
                const double cd = static_cast<double>(c);
                clipped[i] = (std::isnan(d) || std::isnan(cd)) ? std::numeric_limits<double>::quiet_NaN()
                                                               : (cd < d ? cd : d);
            }
            const float mass = to_f32(pairwise(clipped.data(), clipped.size())) / row.total;
            row.kn_term = mass * to_f32(inv_v);
        }
        mc.counts_[ci] = std::move(row);
    }
    return mc;
}

std::vector<long long> MarkovChain::encode(const std::vector<U32>& tokens) const {
    const long long unk = tok2id_.at(U"<UNK>");
    std::vector<long long> out;
    out.reserve(tokens.size());
    for (const auto& t : tokens) {
        const auto it = tok2id_.find(t);
        out.push_back(it == tok2id_.end() ? unk : it->second);
    }
    return out;
}

std::vector<U32> MarkovChain::decode(const std::vector<long long>& ids) const {
    const long long n = static_cast<long long>(id2tok_.size());
    std::vector<U32> out;
    out.reserve(ids.size());
    for (long long i : ids) out.push_back((0 <= i && i < n) ? id2tok_[static_cast<size_t>(i)] : U"<UNK>");
    return out;
}

long long MarkovChain::ctx_index(const std::vector<long long>& ids) const {
    const long long v = static_cast<long long>(id2tok_.size());
    long long idx = 0;
    // ctx_ids[-self.order:]: order 0 slices from index 0, so the whole list.
    const size_t start =
        order_ > 0 && ids.size() > static_cast<size_t>(order_) ? ids.size() - static_cast<size_t>(order_) : 0;
    for (size_t i = start; i < ids.size(); ++i) idx = idx * v + ids[i];
    return idx;
}

double MarkovChain::row_prob(const Row& row, long long next_id) const {
    if (next_id < 0 || static_cast<size_t>(next_id) >= row.counts.size())
        throw IndexError("index " + std::to_string(next_id) + " is out of bounds");
    const float c = row.counts[static_cast<size_t>(next_id)];
    if (kneser_ney_) {
        const double d = c >= 3 ? kn_d3_ : c == 2 ? kn_d2_ : c == 1 ? kn_d1_ : 0.0;
        const double diff = static_cast<double>(c) - d;
        // np.maximum propagates NaN.
        const double kept = std::isnan(diff) ? diff : (diff < 0.0 ? 0.0 : diff);
        return kept / static_cast<double>(row.total) + static_cast<double>(row.kn_term);
    }
    // Laplace: (row + smoothing) / (row.sum() + smoothing * V), all float32.
    const float s = to_f32(smoothing_);
    const float denom = row.total + to_f32(smoothing_ * static_cast<double>(id2tok_.size()));
    return static_cast<double>((c + s) / denom);
}

double MarkovChain::prob(const std::vector<U32>& context, long long next_id) const {
    std::vector<U32> tail;
    if (order_ == 0) {
        tail = context;  // context[-0:] is the whole list
    } else {
        const size_t n = static_cast<size_t>(order_);
        tail.assign(context.size() > n ? context.end() - static_cast<std::ptrdiff_t>(n) : context.begin(),
                    context.end());
    }
    const auto it = counts_.find(ctx_index(encode(tail)));
    if (it != counts_.end() && it->second.total > 0) return row_prob(it->second, next_id);
    // A loaded model has no lower-order model, so it falls back to uniform.
    return static_cast<double>(to_f32(1.0 / static_cast<double>(id2tok_.size())));
}

double MarkovChain::perplexity(const std::vector<std::vector<U32>>& sequences) const {
    double total_log = 0.0;
    long long total_n = 0;
    const size_t order = static_cast<size_t>(order_);
    for (const auto& seq : sequences) {
        const auto ids = encode(seq);
        for (size_t i = 0; i + order < ids.size(); ++i) {
            const std::vector<long long> ctx(ids.begin() + static_cast<std::ptrdiff_t>(i),
                                             ids.begin() + static_cast<std::ptrdiff_t>(i + order));
            const double p = prob(decode(ctx), ids[i + order]);
            total_log += std::log(1e-30 > p ? 1e-30 : p);
            ++total_n;
        }
    }
    const double x = -total_log / static_cast<double>(total_n > 1 ? total_n : 1);
    const double out = std::exp(x);
    if (std::isinf(out) && std::isfinite(x)) throw Error("OverflowError", "math range error");
    return out;
}

}  // namespace tugaphone::markov
