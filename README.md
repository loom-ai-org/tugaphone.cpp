# tugaphone.cpp

A C++17 port of [tugaphone](https://github.com/TigreGotico/tugaphone), which turns Portuguese text into IPA for the dialects of Portugal, Brazil, Africa and Asia. The output is the same as the Python package, byte for byte. See [PARITY.md](PARITY.md) for the tests and the list of the ported names.

These modules are ported: the dialect registry, the dialect inventories, the text normalization rules and the number reader. The phonemizer itself is not ported yet.

```cpp
#include <tugaphone/number_utils.hpp>
#include <tugaphone/registry.hpp>
#include <tugaphone/text_normalization.hpp>

tugaphone::registry::resolve_lect("pt-pt-x-azores");  // "pt-PT-x-acores"
tugaphone::text_normalization::normalize_orthography("D. Maria II chegou às 16:00.");
// "Dona Maria Segunda chegou às 16 horas."
tugaphone::number_utils::normalize_numbers("Comprei 2 casas.");
// "Comprei duas casas."
```

## Dependencies

The library uses four other ports, which are git submodules under `third_party/`: silabificador.cpp, tugalex.cpp, bifonia.cpp and orthography2ipa.cpp. Clone with `--recursive`, or run `git submodule update --init` after the clone. orthography2ipa.cpp needs the Boost headers and ICU. The tests need zlib.

To use installed copies of the four ports, set `-DTUGAPHONE_USE_SYSTEM_DEPS=ON`. CMake then finds them with `find_package`.

## Data files

The library reads its data at run time. `cmake --install` puts the files in `<prefix>/share/tugaphone`, and the library looks there by default. To use another directory, set the environment variable `TUGAPHONE_DATA_DIR` or call `tugaphone::set_data_dir()` before the first call. The tugalex lexicon has its own data directory and its own variable, `TUGALEX_DATA_DIR`.

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
