"""Write the API inventory tables of PARITY.md from the reference package.

The inventory lists every public name of every tugaphone module: the
module-level functions, classes and constants it defines, and the public
methods, class attributes and instance attributes of each public class.
A name that a module imports from another tugaphone module is counted once,
in the module that defines it. The CLI flags and the data files are listed
too.

Each row maps the name to its C++ name and a status. A module listed in
PORTED has the status of the phase that ports it, and the script checks that
each C++ name is declared under include/. Every other module has the status
"later phase".

PARITY.md holds one section per module. The script replaces the text
between "<!-- inventory:<module> -->" and "<!-- /inventory:<module> -->" and
prints the counts.

Run it with the reference interpreter:
    python -I tools/inventory.py PARITY.md
"""
import ast
import pathlib
import re
import sys

import tugaphone

PKG = pathlib.Path(tugaphone.__file__).parent
ROOT = pathlib.Path(__file__).resolve().parent.parent
PORTED = {"registry": "phase 1", "dialects": "phase 1", "text_normalization": "phase 1", "number_utils": "phase 1",
          "version": "phase 1", "langdetect": "phase 3", "codeswitch": "phase 3", "accent": "phase 3",
          "tokenizer": "phase 2"}
# Python names whose C++ form is not <namespace>::<name>.
CPP_NAMES = {
    ("dialects", "LEXICON"): "tugaphone::dialects::LEXICON()",
    ("dialects", "AO1990"): "tugaphone::dialects::AO1990()",
    ("version", "__version__"): "tugaphone::version::version",
    ("langdetect", "LANGS"): "tugaphone::langdetect::langs()",
    ("langdetect", "PORTUGUESE_KEEP"): "tugaphone::langdetect::portuguese_keep()",
    ("accent", "DEFAULT_RESPELL_RULES"): "tugaphone::accent::default_respell_rules()",
    ("accent", "RespellRule.name"): "tugaphone::accent::RespellRule::name()",
    ("accent", "RespellRule.pattern"): "tugaphone::accent::RespellRule::pattern()",
    ("accent", "RespellRule.replacement"): "tugaphone::accent::RespellRule::replacement()",
    ("accent", "RespellRule.note"): "tugaphone::accent::RespellRule::note()",
    ("accent", "Transform.to_dict"): "tugaphone::accent::Transform::to_dict_json",
    ("accent", "Transform.from_dict"): "tugaphone::accent::Transform::from_dict_json",
    ("accent", "AccentOverlay.to_dict"): "tugaphone::accent::AccentOverlay::to_dict_json",
    ("accent", "AccentOverlay.from_dict"): "tugaphone::accent::AccentOverlay::from_dict_json",
    ("tokenizer", "PRONOUNCED_QU_GU_U"): "tugaphone::tokenizer::PRONOUNCED_QU_GU_U()",
}
CALLABLE_SETS = {"LABEL_HEADS", "PLURAL_FUNCTION_WORDS", "FEMININE_DETERMINERS", "MASCULINE_DETERMINERS",
                 "FUNCTION_WORDS", "RANGE_LINKS", "UNIT_NAMES"}


def public(name):
    return not name.startswith("_") or name == "__version__"


def targets(node):
    if isinstance(node, ast.Assign):
        return [t.id for t in node.targets if isinstance(t, ast.Name)]
    if isinstance(node, ast.AnnAssign) and isinstance(node.target, ast.Name):
        return [node.target.id]
    return []


def class_rows(cls):
    rows = []
    for node in cls.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            if public(node.name):
                rows.append((f"{cls.name}.{node.name}", "method"))
            if node.name in ("__init__", "__post_init__"):
                for sub in ast.walk(node):
                    if isinstance(sub, ast.Assign):
                        for t in sub.targets:
                            if (isinstance(t, ast.Attribute) and isinstance(t.value, ast.Name)
                                    and t.value.id == "self" and public(t.attr)):
                                rows.append((f"{cls.name}.{t.attr}", "attribute"))
        for name in targets(node):
            if public(name):
                rows.append((f"{cls.name}.{name}", "attribute"))
    seen = {}
    for name, kind in rows:
        seen.setdefault(name, kind)
    return list(seen.items())


def module_rows(path):
    tree = ast.parse(path.read_text(encoding="utf-8"))
    rows = []
    for node in tree.body:
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and public(node.name):
            rows.append((node.name, "function"))
        elif isinstance(node, ast.ClassDef) and public(node.name):
            rows.append((node.name, "class"))
            rows += class_rows(node)
        else:
            rows += [(n, "constant") for n in targets(node) if public(n)]
    seen = {}
    for name, kind in rows:
        seen.setdefault(name, kind)
    return list(seen.items())


def cpp_name(module, name):
    if (module, name) in CPP_NAMES:
        return CPP_NAMES[(module, name)]
    head, _, member = name.partition(".")
    if member in CALLABLE_SETS:
        return f"tugaphone::{module}::{head}::{member}()"
    return f"tugaphone::{module}::{name.replace('.', '::')}"


def declared(cpp):
    last = cpp.rstrip("()").split("::")[-1]
    headers = "".join(p.read_text(encoding="utf-8") for p in (ROOT / "include").rglob("*.hpp"))
    return re.search(rf"\b{re.escape(last)}\b", headers) is not None


def module_table(module):
    path = PKG / f"{module}.py"
    lines = ["| Python name | Kind | C++ name | Status |", "| --- | --- | --- | --- |"]
    counts = {}
    for name, kind in module_rows(path):
        if module in PORTED:
            cpp = cpp_name(module, name)
            if not declared(cpp):
                raise SystemExit(f"{module}.{name}: {cpp} is not declared under include/")
            status = PORTED[module]
            cell = f"`{cpp}`"
        else:
            status, cell = "later phase", "none yet"
        counts[status] = counts.get(status, 0) + 1
        lines.append(f"| `{name}` | {kind} | {cell} | {status} |")
    return lines, counts


def cli_table():
    source = (PKG / "__main__.py").read_text(encoding="utf-8")
    lines = ["| Python name | Kind | C++ name | Status |", "| --- | --- | --- | --- |"]
    items = [(f"tugaphone {c}", "command") for c in re.findall(r'add_parser\(\s*"([a-z-]+)"', source)]
    items += [(f"{a}", "flag") for a in dict.fromkeys(re.findall(r'add_argument\(\s*"(--[a-z-]+)"', source))]
    for name, kind in items:
        lines.append(f"| `{name}` | {kind} | none yet | later phase |")
    return lines, {"later phase": len(items)}


def data_readers(rel):
    """Modules that open data/<rel>: their source quotes its first path component."""
    quoted = '"' + rel.split("/")[0] + '"'
    readers = {p.stem for p in PKG.glob("*.py") if quoted in p.read_text(encoding="utf-8")}
    if not readers:
        raise SystemExit(f"no module of the package reads data/{rel}")
    return readers


def data_table():
    lines = ["| Python name | Kind | C++ name | Status |", "| --- | --- | --- | --- |"]
    counts = {}
    for path in sorted((PKG / "data").rglob("*")):
        if path.is_file():
            rel = path.relative_to(PKG / "data").as_posix()
            ours = ROOT / "data" / rel
            readers = data_readers(rel)
            verbatim = ours.is_file() and ours.read_bytes() == path.read_bytes()
            status = max(PORTED[r] for r in readers) if verbatim and readers <= PORTED.keys() else "later phase"
            counts[status] = counts.get(status, 0) + 1
            lines.append(f"| `data/{rel}` | data file, read by {', '.join(sorted(readers))} | `data/{rel}` (verbatim) "
                         f"| {status} |")
    return lines, counts


def main(parity_path):
    text = pathlib.Path(parity_path).read_text(encoding="utf-8")
    modules = sorted(p.stem for p in PKG.glob("*.py") if p.stem != "__main__")
    tables = {m: module_table(m) for m in modules}
    tables["cli"] = cli_table()
    tables["data"] = data_table()
    total = {}
    for key, (lines, counts) in tables.items():
        begin, end = f"<!-- inventory:{key} -->", f"<!-- /inventory:{key} -->"
        if begin not in text:
            raise SystemExit(f"PARITY.md has no section marker for {key}")
        pre, rest = text.split(begin, 1)
        _, post = rest.split(end, 1)
        text = pre + begin + "\n" + "\n".join(lines) + "\n" + end + post
        for status, n in counts.items():
            total[status] = total.get(status, 0) + n
        print(f"{key}: " + ", ".join(f"{n} {s}" for s, n in sorted(counts.items())))
    pathlib.Path(parity_path).write_text(text, encoding="utf-8")
    print("total: " + ", ".join(f"{n} {s}" for s, n in sorted(total.items())) + f", {sum(total.values())} names")


if __name__ == "__main__":
    main(sys.argv[1])
