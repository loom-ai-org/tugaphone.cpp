# tugaphone.cpp

A C++17 port of [tugaphone](https://github.com/TigreGotico/tugaphone), which turns Portuguese text into IPA for the dialects of Portugal, Brazil, Africa and Asia. The output is the same as the Python package 1.6.2a1, byte for byte. See [PARITY.md](PARITY.md) for the tests and the list of the ported names.

The whole package is ported: the lattice phonemizer, the tokenizer, the dialect registry and inventories, text normalization, the number reader, the language detector, code-switching, accent forcing, the G2P plugin classes and the command line.

```cpp
#include <tugaphone/tugaphone.hpp>

tugaphone::TugaPhonemizer ph;
ph.phonemize_sentence("O comboio chegou às 16:30.", "pt-PT");
ph.phonemize_sentence("Vou ao shopping.", "pt-BR", "en");  // "en" reads English loanwords as English
tugaphone::force_accent("o vinho verde", "pt-PT-x-porto");
tugaphone::resolve_lect("pt-pt-x-azores");  // "pt-PT-x-acores"
```

## Command line

The executable `tugaphone` takes the same commands, flags and output as `python -m tugaphone`:

```
tugaphone phonemize "Tenho 23 anos, Sr. Silva; li o Cap. XII." --lect pt-BR
tugaphone force-accent "o vinho verde" --lect pt-PT-x-porto --mode respell
tugaphone list
```

The build also makes `tugaphone_benchmark`, the port of `scripts/benchmark.py`. It scores the rules-only transcription against the gold fixtures in `tests/data/gold/` and writes `benchmarks/results.json` and `docs/scoreboard.md`, as the Python script does. The fixtures are samples of the TigreGotico/portuguese-unified-pronunciation-lexicon dataset, under its CC BY-SA 4.0 licence. The tool is not installed. To run it from a build tree, give it the repository root and the three data directories of the source tree:

```
TUGAPHONE_BENCHMARK_ROOT=$PWD \
TUGAPHONE_DATA_DIR=$PWD/data \
TUGALEX_DATA_DIR=$PWD/third_party/tugalex.cpp/data \
BIFONIA_DATA_DIR=$PWD/third_party/bifonia.cpp/data \
build/tugaphone_benchmark --limit 20
```

Without `TUGAPHONE_BENCHMARK_ROOT`, the tool uses the working directory as the root. Without the data variables, the library reads the installed data, so the tool works from a build tree only after `cmake --install`.

## Dependencies

The library uses four other ports, which are git submodules under `third_party/`: silabificador.cpp, tugalex.cpp, bifonia.cpp and orthography2ipa.cpp. Clone with `--recursive`, or run `git submodule update --init` after the clone. orthography2ipa.cpp needs the Boost headers and ICU. The library needs zlib to read the language models.

A project that uses the installed package must also find Boost, because the installed orthography2ipa configuration calls `find_dependency(Boost)`. If Boost is not in a default location, set `BOOST_ROOT`.

To use installed copies of the four ports, set `-DTUGAPHONE_USE_SYSTEM_DEPS=ON`. CMake then finds them with `find_package`.

## Data files

The library reads its data at run time. `cmake --install` puts the files in `<prefix>/share/tugaphone`, and the library looks there by default. The library finds that directory from the path of the program or the shared library that holds it, so an install that `cmake --install --prefix` puts in another prefix finds its own data. The tugalex, bifonia and orthography2ipa data follow the same rule. When that directory does not hold the data, the library uses the install location that the build compiles in. To use another directory, set the environment variable `TUGAPHONE_DATA_DIR` or call `tugaphone::set_data_dir()` before the first call. The tugalex lexicon has its own data directory and its own variable, `TUGALEX_DATA_DIR`. The bifonia homograph tables have `BIFONIA_DATA_DIR`. The tests set all three to the copies in the source trees.

The pt-PT and pt-BR engines read their lexicon from `o2i-lexicons/<lect>/` in this data directory. The other lects have no lexicon. If the file of a lect is missing, the first call for that lect fails with LookupError. The entries are licensed CC-BY-SA-4.0; `<source>.meta.json` beside each file names the source, and `LICENSE.nilc` holds the licence of the pt-BR source.

The orthography2ipa data has its own variable, `ORTHOGRAPHY2IPA_DATA_DIR`, and its own call, `orthography2ipa::set_data_directory()`. The registry gets its list of lects from this data. If the directory is missing, the library does not stop: `list_dialects()` returns an empty list, and each code resolves to the default lect "pt-PT". The Python package does the same when its orthography2ipa data is missing. If `list_dialects()` is empty, check `ORTHOGRAPHY2IPA_DATA_DIR`.

## Threads

`phonemize()` and the tokenizer can be called from more than one thread. orthography2ipa.cpp has no lock of its own, so the library holds one lock while it transcribes. `lattice_core::engine()` returns an engine that is shared. Do not call its methods while another thread calls into the library.

## Build

```
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build --prefix /usr/local
```

To use the library from another CMake project:

```cmake
find_package(tugaphone REQUIRED)
target_link_libraries(app PRIVATE tugaphone::tugaphone)
```

## Licence

Apache-2.0, the same licence as the Python package. See [LICENSE](LICENSE). The lexicon files under `data/o2i-lexicons/` are CC-BY-SA-4.0, as their sidecar files state.
