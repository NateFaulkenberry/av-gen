#pragma once

// ADR-1702: NRPN and RPN decoding from the CC stream. A synth that sends a knob as NRPN sends four
// CCs per change -- 99 (parameter MSB), 98 (parameter LSB), 6 (value MSB), 38 (value LSB) -- and
// 101/100 select an RPN the same way. This turns each completed value into one Nrpn/Rpn message
// carrying the 14-bit parameter number, and marks the CCs that belong to a sequence
// (kMidiNrpnPart) so MIDI Learn never captures 99 or 6 when the person turned one knob.
//
// The rules, per device and channel:
//   * 99/98 select an NRPN, 101/100 an RPN; RPN 127/127 is the null parameter and deselects.
//     A part not sent since the selection kind changed counts as 0.
//   * 6 (data entry MSB) emits the value MSB << 7. 38 (data entry LSB) emits it with the LSB and
//     marks the parameter 14-bit from then on. 96/97 step the value by one unit of its resolution.
//   * The value is 7-bit until an LSB arrives for that parameter: a 7-bit device's 127 is 1.0
//     (MidiMessage::normalized), not 0.992. Selecting a different parameter starts over.
//   * Data entry with nothing selected is an ordinary CC 6/38/96/97 and is not marked.
//   * Every other CC passes through untouched and does not disturb a selection.
// Which parameter a number means on a given synth is that synth's business (an optional profile),
// not this decoder's.

#include "control/midi.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace avgen::control {

class NrpnDecoder {
public:
    // Feeds one message. For a CC that belongs to a sequence, sets kMidiNrpnPart on `message`;
    // returns the decoded message when this CC completes (or changes) a value.
    std::optional<MidiMessage> feed(MidiMessage& message);
    void reset();
    void resetDevice(std::uint16_t device);

private:
    enum class Select : std::uint8_t { None, Nrpn, Rpn };
    struct Channel {
        Select select = Select::None;
        std::uint8_t msb = 0;
        std::uint8_t lsb = 0;
        std::uint16_t valueParam = 0xFFFF; // the parameter `value` and `hiRes` belong to
        Select valueSelect = Select::None;
        std::uint16_t value = 0;
        bool hiRes = false;
    };
    using Device = std::array<Channel, 16>;
    Device& device(std::uint16_t slot);

    std::vector<Device> devices_; // by device slot; grows when a new slot appears, never per event
    Device unknown_{};            // messages with no device (built by hand)
};

} // namespace avgen::control
