#include "tugaphone/version.hpp"

#include "common.hpp"
#include "modules.hpp"

namespace parity {

namespace vs = tugaphone::version;

std::string record_version(const Value& v) {
    const std::string& fn = v[0].str;
    if (fn != "constant") return "unknown call " + fn;
    const std::string& name = v[1].str;
    std::string value;
    if (name == "VERSION_MAJOR") value = std::to_string(vs::VERSION_MAJOR);
    else if (name == "VERSION_MINOR") value = std::to_string(vs::VERSION_MINOR);
    else if (name == "VERSION_BUILD") value = std::to_string(vs::VERSION_BUILD);
    else if (name == "VERSION_ALPHA") value = std::to_string(vs::VERSION_ALPHA);
    else if (name == "__version__") value = J(std::string(vs::version));
    else return "unknown constant " + name;
    return "[" + J(fn) + "," + J(name) + "," + value + "]";
}

}  // namespace parity
