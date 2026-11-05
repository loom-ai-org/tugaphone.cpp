// orthography2ipa keeps its spec cache, resolution catalog, lexicon registry
// and plugin registry in global state with no lock. Every tugaphone call into
// orthography2ipa holds this mutex.
#pragma once

#include <mutex>

namespace tugaphone::detail {

std::mutex& o2i_registry_mutex();

}  // namespace tugaphone::detail
