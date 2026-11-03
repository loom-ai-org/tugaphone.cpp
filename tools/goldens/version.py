"""Goldens of tugaphone.version."""
from tugaphone import version


def corpora(repo):
    names = ["VERSION_MAJOR", "VERSION_MINOR", "VERSION_BUILD", "VERSION_ALPHA", "__version__"]
    return [("constants", [["constant", name, getattr(version, name)] for name in names])]
