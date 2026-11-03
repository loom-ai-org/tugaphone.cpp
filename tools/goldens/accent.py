"""Goldens of tugaphone.accent.

The respelling hill-climb, force_accent and the overlay run against a
deterministic stand-in for the lattice phonemizer (fake_phonemize), which
tests/parity/accent.cpp repeats code point for code point. The default rule
matchers run over every gold-TSV word. Transform and user RespellRule
patterns exercise the re-compatible engine over a pattern corpus that
includes invalid patterns and templates.
"""
import json
import random
import re

import tugaphone.accent as accent
from tugaphone.registry import list_dialects

from . import common, contact_corpus

BASE = [("tch", "tʃ"), ("dj", "dʒ"), ("ch", "ʃ"), ("lh", "ʎ"), ("nh", "ɲ"), ("ç", "s"), ("ê", "e"), ("ô", "o"),
        ("ei", "ɐj"), ("ou", "ow")]


def family(lect):
    if "porto" in lect or "braga" in lect or "minho" in lect:
        return "north"
    if lect.startswith("pt-BR"):
        return "br"
    if lect.startswith("pt-AO") or lect.startswith("pt-MZ"):
        return "africa"
    return "base"


def fake_phonemize(text, lect):
    t = text.lower()
    fam = family(lect)
    if fam == "north":
        t = t.replace("v", "b")
    if fam == "br":
        t = t.replace("ti", "tʃi").replace("di", "dʒi").replace("ei", "e").replace("ou", "o")
        if t.endswith("te"):
            t = t[:-2] + "tʃi"
        if t.endswith("de"):
            t = t[:-2] + "dʒi"
        if t.endswith("l"):
            t = t[:-1] + "w"
        t = t.replace("l ", "w ")
    if fam == "africa":
        t = t.replace("ou", "o")
    for a, b in BASE:
        t = t.replace(a, b)
    return t


SUBJECTS = [
    "", "a", "abc", "ABC", "aAbB", "o carro", "ʀ ʁ ʀʀ", "kaˈʀu", "The quick brown fox", "x\n", "\n", "a\nb\n",
    "ſ s S", "K k K", "µ μ Μ", "İ i ı I", "Σσς", "ΐ ΐ ΰ", "straße STRASSE", "١٢٣ 123 ² ½", "_a_ a_b", "😀a😀",
    "\U0001F600\U0001F64F", "aaa", "abab", "xyz", "ação Ação AÇÃO", "dente", "sal", "vinho Vinho", "lh nh",
    "a\tb", "abcd", "ac", "ab\nab", "binho", "a.b", "a-b", "o vinho, sim!", "ǅ ǆ Ǆ", "ﬁ", "\u0000x", "e\u0301",
    "Ⅻ ⅻ", "𐐀 𐐨", "ꭰ Ꭰ", "ẞ ß",
]

PATTERNS = [
    # literals and escapes
    "a", "ab", "A", "ʀ", "\\.", "\\n", "\\t", "\\x41", "\\u0041", "\\U0001F600", "\\101", "\\0", "\\07", "\\-", "\\é",
    "\\\\", "\\a", "\\f", "\\v", "\\r", "\\ ", "ß", "SS", "ǅ", "ſ", "K", "µ", "Σ", "ς", "İ", "ı", "ΐ", "ﬁ", "𐐀",
    "ꭰ", "ẞ", "e\u0301", "\\x00",
    # classes
    "[abc]", "[a-c]", "[^a]", "[^ab]", "[a\\-z]", "[]a]", "[^]a]", "[a-]", "[-a]", "[\\w]", "[\\W\\d]", "[\\s\\S]",
    "[^\\w]", "[A-Z]", "[a-z]", "[ſ]", "[K]", "[k]", "[µ]", "[σ]", "[İ]", "[i]", "[\\u0100-\\u017f]",
    "[\\U0001F600-\\U0001F64F]", "[\\x00-\\U0010ffff]", "[à-ÿ]", "[0-9]", "[\\b]", "[\\101-\\132]", "[.]", "[$^]",
    "[\\]]", "[a-a]", "[𐐀-𐐏]", "[𐐨-𐑏]", "[Ꭰ-Ᏼ]", "[ꭰ-ꮿ]", "[^\\W\\d_]", "[ǅ]", "[ẞ]", "[\\x41-\\x5a\\d]",
    # quantifiers
    "a*", "a+", "a?", "a{2}", "a{1,2}", "a{,2}", "a{2,}", "a*?", "a+?", "a??", "a{1,2}?", "a*+", "a++", "a?+",
    "(?:ab)*", "(?:a|b)*c", "x*", "x*?", "(a?)*", "(a*)*", "(a*)+", "(a|)*b", "(a|b)*?b", "a{", "a{1", "a{1,",
    "a{x}", "{", "a{,}", "(?:)*", "(?=a)*", "\\b*", "a{0}", "(a){0}", "(?:a?){3}+", "(?:a|ab)++c",
    # anchors
    "^", "$", "^a", "a$", "\\Aa", "a\\Z", "\\b", "\\B", "\\ba", "a\\b", "\\bo\\b", "(?m)^a", "(?m)$", "(?m)^",
    "(?s).", ".", "\\b\\b", "^$", "$\\n", "\\Z", "\\A",
    # groups
    "(a)(b)", "(?P<x>a)(?P=x)", "(a)\\1", "(a)|b", "(?:(a)|b)", "((a)|b)*", "(?=a)", "(?!a)", "(?<=a)b",
    "(?<!a)b", "(?<=ab)c", "(?<=a|b)c", "(?<=a|bc)", "(?<=a*)", "(?>a*)a", "(?>a|ab)c", "(a)(?(1)b|c)",
    "(?(1)a|b)", "(a)?(?(1)b|c)", "(?P<n>a)?(?(n)b|c)", "(?#comment)a", "(?i)a", "(?i:a)A", "(?-i:a)",
    "a(?i)b", "(?x) a b # c", "(?x)[ ]a", "(?a)\\w+", "(?u)\\w+", "(?a:\\w)\\w", "(?au)a", "(?L)a", "(?i-i:a)",
    "(?-a:x)", "(?z)", "(?i)[^a]", "(?i)[a-z]+", "(?i)ß", "(a)(?=(b))", "(?!(a)b)a", "(a)\\1+", "(?i)(a)\\1",
    "(?<=(a))b", "(?<=\\b)a", "(?<!^)a", "(a)(?<=\\1)", "(?P<a1>x)", "(?s:.)\\n",
    # errors
    "(", ")", "[", "[a", "a**", "*", "+a", "?", "\\", "a\\", "(?P<1>a)", "(?P<x>a)(?P<x>b)", "(?P=y)", "\\2",
    "(a)\\2", "\\q", "[\\q]", "[z-a]", "[\\w-z]", "\\x4", "\\u00", "\\U00110000", "\\400", "(?<", "(?<x", "(?",
    "(?P", "(?Px)", "a{3,2}", "a{4294967295}", "{1}", "(a)(?(2)b)", "(?(0)a)", "(?(a)b)", "(a)(?(1)b|c|d)",
    "(?<=(?P<g>a)(?P=g))", "(?i", "(?i-", "(?-)", "(?i-:a)", "[\\d-z]", "(?#unterminated", "(?<=a)(?<=(b)\\1)",
    # alternation and Unicode classes
    "a|b", "a|", "|a", "ab|a", "a|ab", "(a|ab)(c|bcd)(d*)", "\\w+", "\\d+", "\\s+", "\\W+", "[^\\W\\d_]+", "\\D+",
    "\\S+",
]

REPLS = ["X", "<\\g<0>>", "", "\\n", "\\\\", "\\-", "\\q", "\\0", "\\07", "\\101", "\\400", "é", "$0", "\\é",
         "\\g<", "\\g<>", "\\g<1>", "\\1", "\\g<x>", "\\12", "\\g<-1>", "\\g<a b>", "\\g<٣>"]

LONG = [("a*", "a"), (".*", "x"), ("(a|b)*", "ab"), ("(?:ɐ|a)+", "ɐa"), ("(a|b)*?$", "ab"), ("(?:ab)+", "ab"),
        ("\\w+", "aé"), ("(?>a|b)*", "ab"), ("(?:a|b)*+", "ab"), ("(?<=a)b", "ab"), ("([^ ]+)", "ʀa"),
        ("(?:(a)|b)*", "ab"), ("(?=a)a*", "a")]

WORD_PATTERNS = ["binho", "a", "", "o carro", "a.b", "(", "ç", "Ç", "ab c", "-", "lh", "VINHO", "ß", "ſ"]


def rule_matches(word):
    return [accent.RespellRule(r.name, r.pattern, r.replacement).matches(word) for r in accent.DEFAULT_RESPELL_RULES]


def overlays():
    texts = [
        '{"name":"v","transforms":[{"kind":"regex","pattern":"ʀ","replacement":"ɾ","stage":"ipa"}]}',
        '{"name":"w","transforms":[{"kind":"word","pattern":"binho","replacement":"BINHO","stage":"text"},'
        '{"kind":"regex","pattern":"(?i)o","replacement":"0","stage":"text","ignore_case":false},'
        '{"kind":"regex","pattern":"a","replacement":"b","stage":"ipa"},'
        '{"kind":"regex","pattern":"b","replacement":"c","stage":"ipa"}]}',
        "{}", "[]", '{"transforms": null}', '{"transforms": {}}', '{"transforms": {"a": 1}}', '{"transforms": "ab"}',
        '{"transforms": ""}', '{"name": 5}', '{"name": null}', '{"name": [1, "a", 2.5, true, null, {"k": "v"}]}',
        '{"name": 1e400}', '{"name": -1e400}', '{"name": NaN}', '{"name": -0}', '{"name": 1e-7}', '{"name": 12345678901234567890}',
        '{"name": "é\\u0000\\n\\"\'"}', '{"name": ["it\'s", "say \\"hi\\"", "both \' and \\"", "\\u200b\\u0085\\t\\\\"]}',
        '{"name": {"k\'": ["\\ud83d\\ude00", "\\u00ad", "\\u0007", "\\u00a0"]}}',
        '{"transforms":[{"ignore_case": 0}, {"ignore_case": "false"}, {"ignore_case": []}, {"ignore_case": null},'
        ' {"kind": 1, "pattern": 2.0, "replacement": [], "stage": {}}, {"ignore_case": 0.0}, {"ignore_case": [0]}]}',
        '{"transforms":[[]]}', '{"transforms":[5]}', "not json", '{"name":"x","name":"y"}', '{"name": "😀"}',
        '{"name": true, "transforms": [{"kind": "word", "pattern": "a b", "replacement": "\\\\g<0>!", '
        '"stage": "text", "ignore_case": true}]}',
        '{"name": "\\u2028\\u2029\\ufeff\\U0001d7ce"}', '{"name": "", "transforms": [], "extra": 1}',
        # The overlays of tests/test_accent.py.
        '{"transforms":[{"kind":"regex","pattern":"ʀ","replacement":"ɾ","stage":"ipa"},'
        '{"kind":"regex","pattern":"XXX","replacement":"!","stage":"text"}]}',
        '{"name":"voice-1","transforms":[{"kind":"regex","pattern":"ʀ","replacement":"h","stage":"ipa"},'
        '{"kind":"word","pattern":"lh","replacement":"j","stage":"text","ignore_case":false}]}',
    ]
    return texts


def outputs(o):
    return [json.dumps(o.to_dict(), ensure_ascii=False), o.to_json(), o.to_json(indent=None),
            o.to_json(ensure_ascii=True), o.to_json(indent=0), o.to_json(indent=4)]


def corpora(repo):
    rows = contact_corpus.gold_rows(repo)
    gold = common.unique(w for _, w in rows)
    words = contact_corpus.words(repo, gold=False)
    sentences = contact_corpus.sentences(repo)
    saved = accent.phonemize
    accent.phonemize = fake_phonemize
    try:
        consts = [["default_rules", [[r.name, r.pattern, r.replacement, r.note] for r in accent.DEFAULT_RESPELL_RULES]]]
        rules = [["rule_matches", w, rule_matches(w)] for w in common.unique(gold + [w.lower() for w in gold] + words)]
        # `$` matches before a final newline, so a newline-final word reaches it.
        endings = [w + "\n" for w in gold[:2000]] + ["mal\n", "sal\n\n", "l\n", "dente\n", "Brasil\n", "al\nl\n"]
        rules += [["rule_matches", w, rule_matches(w)] for w in common.unique(endings)]
        helpers = []
        rng = random.Random(1717)
        pool = gold[:400] + common.UNICODE_PROBES
        for _ in range(3000):
            a, b = rng.choice(pool), rng.choice(pool)
            helpers.append(["lev", a, b, accent._levenshtein(a, b)])
        for a in pool[:200]:
            helpers.append(["lev", a, fake_phonemize(a, "pt-BR"), accent._levenshtein(a, fake_phonemize(a, "pt-BR"))])
        for orig in common.UNICODE_PROBES + ["Vinho", "vinho", "Ǆx", "ǅx", "ẞa", "ßa", "İs", "Ⅻ"]:
            for resp in ["binho", "ßx", "ǆa", "ﬁx", "", "ŉ", "i̇x"]:
                helpers.append(["restore_case", orig, resp, accent._restore_case(orig, resp)])
        for text in sentences + common.UNICODE_PROBES + ["a1b2_c", "²x", "x²y", "ab_cd", "Ⅻab"]:
            helpers.append(["word_spans", text, [[m.start(), m.end()] for m in accent._WORD_RE.finditer(text)]])

        respelled = []
        for lect, w in rows:
            respelled.append(["respell_word", w, lect, "pt-PT", accent.respell_word(w, lect, "pt-PT")])
            respelled.append(["respell_word", w, "pt-PT-x-porto", "pt-PT", accent.respell_word(w, "pt-PT-x-porto")])
        lects = list_dialects()
        for i, w in enumerate(words):
            target = lects[i % len(lects)]
            base = lects[(i * 7) % len(lects)] if i % 3 == 0 else "pt-PT"
            respelled.append(["respell_word", w, target, base, accent.respell_word(w, target, base)])
        for i, s in enumerate(sentences):
            target = lects[i % len(lects)]
            respelled.append(["respell", s, target, "pt-PT", accent.respell(s, target, "pt-PT")])
        custom = [accent.RespellRule("x", "o\\b", "u"), accent.RespellRule("y", "(?<=[aeiou])s(?=[aeiou])", "z"),
                  accent.RespellRule("z", "^v", "b"), accent.RespellRule("bad", "(", "x")]
        for w in gold[:500] + ["casa", "vovo", "Vinho", "(", ""]:
            for rule in custom:
                respelled.append(["respell_word_rules", w, "pt-BR", "pt-PT", [[rule.name, rule.pattern, rule.replacement]],
                                  common.call(accent.respell_word, w, "pt-BR", "pt-PT", (rule,))])
            respelled.append(["respell_word_rules", w, "pt-PT-x-porto", "pt-PT",
                              [[r.name, r.pattern, r.replacement] for r in custom[:3]],
                              common.call(accent.respell_word, w, "pt-PT-x-porto", "pt-PT", tuple(custom[:3]))])

        regex = []
        for pattern in PATTERNS:
            for flag in (False, True):
                for i, s in enumerate(SUBJECTS):
                    for repl in ("X", "<\\g<0>>") if i % 2 == 0 else ("X",):
                        t = accent.Transform(kind="regex", pattern=pattern, replacement=repl, ignore_case=flag)
                        regex.append(["transform", "regex", pattern, repl, flag, s, common.call(t.apply, s)])
            for repl in REPLS:
                t = accent.Transform(kind="regex", pattern=pattern, replacement=repl)
                regex.append(["transform", "regex", pattern, repl, True, "abcd aAbB", common.call(t.apply, "abcd aAbB")])
            for s in SUBJECTS[::3]:
                regex.append(["user_rule", pattern, s, common.call(
                    lambda: [list(span) for span in accent.RespellRule("u", pattern, "").matches(s)])])
        for pattern in WORD_PATTERNS:
            for flag in (False, True):
                for s in SUBJECTS + ["o vinho binho", "BINHO binho", "a.b a b", "(a) ( a", "lhlh lh", "ß SS ss"]:
                    t = accent.Transform(kind="word", pattern=pattern, replacement="[\\g<0>]", ignore_case=flag)
                    regex.append(["transform", "word", pattern, "[\\g<0>]", flag, s, common.call(t.apply, s)])
        for kind in ("Regex", "", "words"):
            t = accent.Transform(kind=kind, pattern="a")
            regex.append(["transform", kind, "a", "", True, "a", common.call(t.apply, "a")])

        overlay = []
        for text in overlays():
            o = common.call(accent.AccentOverlay.from_json, text)
            overlay.append(["overlay_json", text, o if isinstance(o, dict) else outputs(o)])
            loaded = common.call(json.loads, text)
            t = loaded if isinstance(loaded, dict) and "err" in loaded else common.call(accent.Transform.from_dict, loaded)
            overlay.append(["transform_json", text,
                            t if isinstance(t, dict) else json.dumps(t.to_dict(), ensure_ascii=False)])
        good = [accent.AccentOverlay.from_json(t) for t in overlays()[:2] + overlays()[-2:]]
        for o in good:
            for s in SUBJECTS + ["o carro ʀ", "o vinho binho"]:
                for stage in ("ipa", "text", "other"):
                    overlay.append(["overlay_apply", o.to_json(), s, stage, common.call(o.apply, s, stage)])

        forced = []
        texts = ["o vinho", "o vinho, sim!", "a tia no Brasil", "o carro", "Vinho verde", "", "dente sal", "pouco peixe"]
        # The inputs of tests/test_accent.py.
        texts += ["x", "gato", "o gato", "o vinho verde e a tia dele no Brasil", "o vinho da vinha"]
        texts += sentences[:150]
        test_overlays = overlays()[:2] + overlays()[-2:]
        for i, text in enumerate(texts):
            for lect in ("pt-BR", "pt-PT-x-porto", "pt-ao", "PT-br", "xx", "pt-PT-x-braga", "pt-PT", "pt-AO",
                         "pt-PT-x-lisbon"):
                for mode in ("ipa", "respell", "nonsense"):
                    ov = test_overlays[i % 4]
                    use = ov if i % 3 else None
                    forced.append(["force_accent", text, lect, mode, "pt-PT", use, common.call(
                        accent.force_accent, text, lect, mode=mode, base_lect="pt-PT",
                        overlay=accent.AccentOverlay.from_json(use) if use else None)])
            forced.append(["force_accent", text, "pt-BR", "respell", "pt-BR", None,
                           common.call(accent.force_accent, text, "pt-BR", mode="respell", base_lect="pt-BR")])
        # Subjects of 100,000 code points: the engine must not need a stack
        # that grows with the subject.
        long_subjects = []
        for pattern, unit in LONG:
            s = unit * (100000 // len(unit))
            t = accent.Transform(kind="regex", pattern=pattern, replacement="X")
            long_subjects.append(["transform", "regex", pattern, "X", True, s, common.call(t.apply, s)])
            long_subjects.append(["user_rule", pattern, s, common.call(
                lambda: [list(span) for span in accent.RespellRule("u", pattern, "").matches(s)])])
    finally:
        accent.phonemize = saved
    return [("constants", consts), ("rule_matches", rules), ("helpers", helpers), ("respell", respelled),
            ("regex", regex), ("overlay", overlay), ("force_accent", forced), ("long_subjects", long_subjects)]
