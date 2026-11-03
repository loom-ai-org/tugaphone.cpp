"""Goldens of the internal markovonnx scorer (MarkovChain.load, _get_probs,
perplexity) over the four bundled langdetect models and synthetic models.

A "model" record loads a model; the records after it score with it. Every
score is a repr string, so the comparison is bit for bit.
"""
import json
import os
import random
import tempfile

from markovonnx import MarkovChain
from tugaphone import langdetect

from . import common, contact_corpus


def load_text(text):
    """MarkovChain.load of a JSON text, through a file as Python reads it."""
    fd, path = tempfile.mkstemp(suffix=".json")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write(text)
        return MarkovChain.load(path)
    finally:
        os.unlink(path)


def summary(mc):
    return [mc.order, mc.vocab.size, bool(mc.kneser_ney), len(mc._counts)]


def synthetic():
    """[(name, json text)]: models that reach the paths the bundled ones do not."""
    rng = random.Random(4242)
    out = []

    def model(order, vocab, counts, **extra):
        data = {"order": order, "smoothing": 1e-05, "backoff": True, "kneser_ney": True,
                "kn_d1": 0.5622925425979198, "kn_d2": 1.0874827797071167, "kn_d3": 1.499151867607795,
                "vocab": {"max_vocab": 0, "id2tok": vocab}, "counts": counts}
        data.update(extra)
        return json.dumps(data)

    small = ["<UNK>", "\x02", "\x03", "a", "b", "c", "ç", "😀"]
    values = [0, 0, 0, 1, 2, 3, 4, 5, 0.5, 2.5, 7.25, 1e-3, 1, 1, 2, 3]

    def rows(v, order, n):
        counts = {}
        for _ in range(n):
            key = rng.randrange(v ** order) if order else 0
            counts[str(key)] = [float(rng.choice(values)) for _ in range(v)]
        return counts

    out.append(("laplace", model(2, small, rows(8, 2, 40), kneser_ney=False, smoothing=0.25)))
    out.append(("laplace-int", model(1, small, rows(8, 1, 6), kneser_ney=False, smoothing=1)))
    out.append(("kn-frac", model(2, small, rows(8, 2, 50))))
    big = ["<UNK>", "\x02", "\x03"] + [chr(0x61 + i) for i in range(26)] + [chr(0x3B1 + i) for i in range(25)] + \
          [chr(0x410 + i) for i in range(32)] + [chr(0x5D0 + i) for i in range(27)] + [chr(0x4E00 + i) for i in range(40)]
    out.append(("kn-wide", model(2, big, rows(len(big), 2, 120))))
    out.append(("kn-int-d", model(2, small, rows(8, 2, 30), kn_d1=1, kn_d2=0, kn_d3=2)))
    out.append(("kn-zero-d", model(2, small, rows(8, 2, 30), kn_d1=0.0, kn_d2=0.0, kn_d3=0.0)))
    out.append(("kn-no-d", json.dumps({"order": 2, "smoothing": 1e-05, "kneser_ney": True,
                                       "vocab": {"id2tok": small}, "counts": rows(8, 2, 20)})))
    out.append(("order0", model(0, small, rows(8, 0, 1))))
    out.append(("order1", model(1, small, rows(8, 1, 8))))
    out.append(("order3", model(3, small, rows(8, 3, 60))))
    dup = '{"order": 1, "smoothing": 1e-05, "kneser_ney": true, "vocab": {"id2tok": ["<UNK>", "a", "b", "a"]}, ' \
          '"counts": {"1": [0, 3, 1, 0], " 1": [0, 1, 1, 1], "+2": [1, 0, 0, 2], "0_3": [2, 2, 2, 2]}}'
    out.append(("dup-keys", dup))
    out.append(("zero-row", model(1, small, {"3": [0.0] * 8, "4": [1.0] * 8})))
    out.append(("bad-json", "{"))
    out.append(("no-vocab", '{"order": 2, "smoothing": 1e-05, "counts": {}}'))
    out.append(("no-order", '{"vocab": {"id2tok": ["<UNK>"]}, "smoothing": 1e-05, "counts": {}}'))
    out.append(("no-counts", '{"order": 2, "smoothing": 1e-05, "vocab": {"id2tok": ["<UNK>"]}}'))
    out.append(("bad-key", '{"order": 1, "smoothing": 1e-05, "vocab": {"id2tok": ["<UNK>", "a"]}, '
                           '"counts": {"x": [1, 2]}}'))
    return out


def sequences(mc, words, rng):
    seqs = [langdetect._wrap(langdetect._normalize(w)) for w in words]
    seqs += [list(w) for w in words[:400]]
    vocab = mc.vocab.id2tok
    seqs += [[rng.choice(vocab) for _ in range(rng.randint(0, 6))] for _ in range(200)]
    seqs += [["<UNK>"], [], ["\x02"], ["\x02", "\x03"], ["xyz", "<UNK>", vocab[-1]]]
    return seqs


def corpora(repo):
    words = contact_corpus.words(repo, gold=False)
    gold = common.unique(w for _, w in contact_corpus.gold_rows(repo))
    det = langdetect.get_detector()
    rng = random.Random(77)
    out = []
    for lang in langdetect.LANGS:
        mc = det._models[lang]
        recs = [["model", lang, None, summary(mc)]]
        for seq in sequences(mc, words + gold, rng):
            recs.append(["perplexity", lang, [seq], repr(mc.perplexity([seq]))])
        for i in range(0, 60, 3):
            group = [langdetect._wrap(langdetect._normalize(w)) for w in gold[i:i + 3]]
            recs.append(["perplexity", lang, group, repr(mc.perplexity(group))])
        vocab = mc.vocab.id2tok
        v = len(vocab)
        keys = sorted(mc._counts)
        for key in keys:
            ctx = mc.vocab.decode([key // v, key % v])
            for nxt in sorted(rng.sample(range(v), 3)):
                recs.append(["prob", lang, ctx, nxt, repr(float(mc._get_probs(ctx)[nxt]))])
        for key in keys[:12]:
            ctx = mc.vocab.decode([key // v, key % v])
            probs = mc._get_probs(ctx)
            recs.extend(["prob", lang, ctx, nxt, repr(float(probs[nxt]))] for nxt in range(v))
        for _ in range(300):
            ctx = [rng.choice(vocab + ["?", "\x00"]) for _ in range(rng.randint(0, 3))]
            nxt = rng.randrange(v)
            recs.append(["prob", lang, ctx, nxt, repr(float(mc._get_probs(ctx)[nxt]))])
        out.append((f"bundled-{lang}", recs))
    recs = []
    for name, text in synthetic():
        mc = common.call(load_text, text)
        if isinstance(mc, dict):
            recs.append(["model", name, text, mc])
            continue
        recs.append(["model", name, text, summary(mc)])
        vocab = mc.vocab.id2tok
        contexts = [[]] + [[a] for a in vocab] + [[a, b] for a in vocab[:12] for b in vocab[:12]]
        contexts += [[a, b, c] for a in vocab[:5] for b in vocab[:5] for c in vocab[:5]]
        for ctx in contexts:
            for nxt in range(min(len(vocab), 12)):
                recs.append(["prob", name, ctx, nxt, common.call(lambda: repr(float(mc._get_probs(ctx)[nxt])))])
        for seq in sequences(mc, words[:300], rng):
            recs.append(["perplexity", name, [seq], common.call(lambda: repr(mc.perplexity([seq])))])
    out.append(("synthetic", recs))
    return out
