// orthography2ipa keeps its spec cache, resolution catalog, lexicon registry
// and plugin registry in global state with no lock. Every tugaphone call that
// can load a spec, fill the resolution catalog or register into those maps
// holds this mutex. The tokenizer calls stress::detect_stress and the vowels
// helpers without it, because they read none of that state.
#pragma once

#include <mutex>

namespace tugaphone::detail {

std::mutex& o2i_registry_mutex();

}  // namespace tugaphone::detail
