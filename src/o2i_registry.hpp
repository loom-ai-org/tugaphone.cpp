// orthography2ipa keeps its spec cache, lexicon registry and plugin registry in
// global maps with no lock. Every tugaphone call that can load a spec or
// register into those maps holds this mutex.
#pragma once

#include <mutex>

namespace tugaphone::detail {

std::mutex& o2i_registry_mutex();

}  // namespace tugaphone::detail
