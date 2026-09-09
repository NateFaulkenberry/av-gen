// MIDI backend for platforms without a native implementation: no devices, opening errors,
// but the shared parser, inject() and drain() keep working (midi_common.cpp).

#include "control/midi.hpp"

#include "control/midi_backend.hpp"

namespace avgen::control {

namespace {

constexpr const char* kNoBackend = "no MIDI backend on this platform";

class StubInputBackend final : public detail::MidiInputBackend {
public:
    Result<void> open(const std::string& /*filter*/) override { return fail(kNoBackend); }
    void close() override {}
    [[nodiscard]] bool isOpen() const override { return false; }
    [[nodiscard]] std::vector<std::string> connectedSources() const override { return {}; }
};

class StubVirtualSourceBackend final : public detail::MidiVirtualSourceBackend {
public:
    Result<void> open(const std::string& /*name*/) override { return fail(kNoBackend); }
    void close() override {}
    [[nodiscard]] bool isOpen() const override { return false; }
    Result<void> send(std::span<const std::uint8_t> /*bytes*/) override { return fail(kNoBackend); }
};

} // namespace

namespace detail {

std::unique_ptr<MidiInputBackend> makeMidiInputBackend(MidiInbox& /*inbox*/) {
    return std::make_unique<StubInputBackend>();
}

std::unique_ptr<MidiVirtualSourceBackend> makeMidiVirtualSourceBackend() {
    return std::make_unique<StubVirtualSourceBackend>();
}

} // namespace detail

std::vector<MidiDeviceInfo> listMidiInputs() {
    return {};
}

bool hasMidiBackend() {
    return false;
}

} // namespace avgen::control
