#include "o2i_registry.hpp"

namespace tugaphone::detail {

std::mutex& o2i_registry_mutex() {
    static std::mutex mutex;
    return mutex;
}

}  // namespace tugaphone::detail
