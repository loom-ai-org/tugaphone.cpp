# tugaphone.cpp

A C++17 port of [tugaphone](https://github.com/TigreGotico/tugaphone), which turns Portuguese text into IPA for the dialects of Portugal, Brazil, Africa and Asia. The output is the same as the Python package, byte for byte, except for the records in `tests/parity/expected_divergences.tsv`. Each of these records waits on a fix in orthography2ipa.cpp, and the list names the pull request of each fix. See [PARITY.md](PARITY.md) for the tests, the fixes and the list of the ported names.

These modules are ported: the dialect registry, the dialect inventories, the text normalization rules, the number reader, the tokenizer, the language detector, the code-switching rules, accent forcing and the phonemizer.

```cpp
#include <tugaphone/lattice_core.hpp>
#include <tugaphone/number_utils.hpp>
#include <tugaphone/registry.hpp>
#include <tugaphone/text_normalization.hpp>

tugaphone::registry::resolve_lect("pt-pt-x-azores");  // "pt-PT-x-acores"
tugaphone::text_normalization::normalize_orthography("D. Maria II chegou às 16:00.");
// "Dona Maria Segunda chegou às 16 horas."
tugaphone::number_utils::normalize_numbers("Comprei 2 casas.");
// "Comprei duas casas."
tugaphone::lattice_core::phonemize("O gato dorme.", "pt-PT");
// "o ˈɡatu ˈdɔɾmɨ"
```

## Dependencies

The library uses four other ports, which are git submodules under `third_party/`: silabificador.cpp, tugalex.cpp, bifonia.cpp and orthography2ipa.cpp. Clone with `--recursive`, or run `git submodule update --init` after the clone. orthography2ipa.cpp needs the Boost headers and ICU. The library needs zlib to read the language models.

With Boost 1.86 and later, the pinned orthography2ipa.cpp does not compile: `get_child` with a temporary default tree is a deleted function. orthography2ipa.cpp pull request 1 (https://github.com/loom-ai-org/orthography2ipa.cpp/pull/1) fixes it. Until the submodule pin includes that fix, use Boost 1.85 or earlier, or apply the change of that pull request to `third_party/orthography2ipa.cpp`. A project that uses the installed package must also find Boost, because the installed orthography2ipa configuration calls `find_dependency(Boost)`. If Boost is not in a default location, set `BOOST_ROOT`.

To use installed copies of the four ports, set `-DTUGAPHONE_USE_SYSTEM_DEPS=ON`. CMake then finds them with `find_package`.

## Data files

The library reads its data at run time. `cmake --install` puts the files in `<prefix>/share/tugaphone`, and the library looks there by default. To use another directory, set the environment variable `TUGAPHONE_DATA_DIR` or call `tugaphone::set_data_dir()` before the first call. The tugalex lexicon has its own data directory and its own variable, `TUGALEX_DATA_DIR`.

The bifonia homograph list has its own variable, `BIFONIA_DATA_DIR`. The orthography2ipa data has its own variable, `ORTHOGRAPHY2IPA_DATA_DIR`, and its own call, `orthography2ipa::set_data_directory()`. The registry gets its list of lects from this data. If the directory is missing, the library does not stop: `list_dialects()` returns an empty list, and each code resolves to the default lect "pt-PT". The Python package does the same when its orthography2ipa data is missing. If `list_dialects()` is empty, check `ORTHOGRAPHY2IPA_DATA_DIR`.

The phonemizer writes the tugalex lexicon of each lect to a cache directory on the first call for that lect. The directory is `TUGAPHONE_LEXICON_DIR` if it is set, else `$XDG_CACHE_HOME/tugaphone/lexicon`, else `~/.cache/tugaphone/lexicon`. Each lexicon file has a stamp with the tugalex release, and a file with another stamp is written again.

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

Apache-2.0, the same licence as the Python package. See [LICENSE](LICENSE).
