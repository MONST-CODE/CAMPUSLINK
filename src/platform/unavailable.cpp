#include "campuslink/network.hpp"

namespace campuslink::net {
namespace {
struct Unavailable final : Backend {
    static Error status() { return {"platform", 0, "Physical Experiment requires Windows x64"}; }
    Snapshot inspect(std::stop_token) override { return {{}, {status()}}; }
    Discovery scan(std::stop_token) override { return {{}, {status()}}; }
    std::optional<Error> connect(const Bss&, std::stop_token) override { return status(); }
    Observation measure(const Interface&, const Scope&, std::stop_token) override { Observation o; o.error = status(); return o; }
};
}
Backend& platform() { static Unavailable backend; return backend; }
}
