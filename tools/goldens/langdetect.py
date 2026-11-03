"""Goldens of tugaphone.langdetect: the normalization and keep-list helpers,
and MarkovLangDetector scores and decisions over single words."""
from tugaphone import langdetect

from . import common, contact_corpus

MARGINS = [0.0, 0.1, 0.25, 0.5, 0.6, 1.0, -0.5, 3.0]
SIDES = [None, "pt", "es", "fr", "en", "xx"]


def scores(d):
    return [[k, repr(v)] for k, v in d.items()]


def detect(det, word, margin, side):
    lang, sc = det.detect(word, margin=margin, default_side=side)
    return [lang, scores(sc)]


def corpora(repo):
    det = langdetect.get_detector()
    words = contact_corpus.words(repo)
    consts = [["langs", langdetect.LANGS], ["default_margin", repr(langdetect.DEFAULT_MARGIN)],
              ["keep", sorted(langdetect.PORTUGUESE_KEEP)],
              ["from_dir", "/nonexistent/langdetect", langdetect.MarkovLangDetector.from_dir(
                  __import__("pathlib").Path("/nonexistent/langdetect"))]]
    helpers = [["word", w, langdetect._normalize(w), langdetect._wrap(w), langdetect.fold_diacritics(w),
                langdetect.is_keep_word(w)] for w in words]
    score = [["score", w, scores(det.score(w))] for w in words]
    default = [["detect", w, repr(langdetect.DEFAULT_MARGIN), None, detect(det, w, langdetect.DEFAULT_MARGIN, None)]
               for w in words]
    grid = []
    for w in words[::12]:
        for m in MARGINS:
            for side in SIDES:
                grid.append(["detect", w, repr(m), side, detect(det, w, m, side)])
            grid.append(["is_contact", w, repr(m), det.is_contact(w, margin=m)])
    return [("constants", consts), ("helpers", helpers), ("score", score), ("detect", default), ("grid", grid)]
