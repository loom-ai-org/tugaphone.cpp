"""Goldens of tugaphone.registry."""
import warnings

from tugaphone import registry

from . import common


def codes(repo):
    lects = registry.list_dialects()
    aliases = list(registry._ALIASES)
    base = lects + aliases + list(registry._LEXICON_REGION) + ["", "pt", "PT", "x", "-", "pt-", "-pt", "pt--PT",
                                                             "pt-PT-x-unknown", "pt-BR-x-unknown-more", "en", "es-ES",
                                                             "pt_PT", "pt_BR", "PT-pt-X-PORTO", " pt-br ", "pt-x-br",
                                                             "pt-PT-X-lisboa", "pt-pt-x-AZORES", "ext-PT", "roa"]
    variants = []
    for code in base:
        variants += [code, code.lower(), code.upper(), code.swapcase()]
    from_tests = [s for s in common.test_strings(repo) if len(s) <= 40 and ("-" in s or s[:2].lower() == "pt")]
    probes = [p + "-" + p for p in common.UNICODE_PROBES] + ["pt-" + p for p in common.UNICODE_PROBES]
    fuzz = common.fuzz_strings(11, 400, "ptPTbrBRxX-_ aoAOmzMZtlTL", 16)
    return [("codes", common.unique(variants)), ("tests", common.unique(from_tests)),
            ("unicode", common.unique(probes)), ("fuzz", common.unique(fuzz))]


def record(code):
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        resolved = common.call(registry.resolve_dialect, code)
    inventory = common.call(registry.get_dialect_inventory, code)
    if not isinstance(inventory, dict):
        inventory = [type(inventory).__name__, inventory.dialect_code]
    return ["code", code, common.call(registry.normalize_dialect_code, code), common.call(registry.resolve_lect, code),
            resolved, common.call(registry.lexicon_region, code), common.call(registry.default_contact, code),
            inventory]


def corpora(repo):
    out = [("list", [["list_dialects", registry.list_dialects()]])]
    for name, items in codes(repo):
        out.append((name, [record(c) for c in items]))
    return out
