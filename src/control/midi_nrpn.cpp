#include "control/midi_nrpn.hpp"

#include <algorithm>

namespace avgen::control {

NrpnDecoder::Device& NrpnDecoder::device(std::uint16_t slot) {
    if (slot >= kAllMidiDevices) {
        return unknown_;
    }
    if (slot >= devices_.size()) {
        devices_.resize(static_cast<std::size_t>(slot) + 1);
    }
    return devices_[slot];
}

void NrpnDecoder::reset() {
    devices_.clear();
    unknown_ = Device{};
}

void NrpnDecoder::resetDevice(std::uint16_t slot) {
    if (slot >= kAllMidiDevices) {
        unknown_ = Device{};
        return;
    }
    if (slot < devices_.size()) {
        devices_[slot] = Device{};
    }
}

std::optional<MidiMessage> NrpnDecoder::feed(MidiMessage& message) {
    if (message.kind != MidiKind::ControlChange || message.channel > 15) {
        return std::nullopt;
    }
    Channel& ch = device(message.device)[message.channel];
    const std::uint8_t v = message.data2;
    switch (message.data1) {
    case 99: // NRPN parameter MSB
    case 98: // NRPN parameter LSB
    case 101: // RPN parameter MSB
    case 100: { // RPN parameter LSB
        const Select kind = (message.data1 == 99 || message.data1 == 98) ? Select::Nrpn : Select::Rpn;
        if (ch.select != kind) {
            ch.msb = 0;
            ch.lsb = 0;
        }
        ch.select = kind;
        if (message.data1 == 99 || message.data1 == 101) {
            ch.msb = v;
        } else {
            ch.lsb = v;
        }
        if (kind == Select::Rpn && ch.msb == 127 && ch.lsb == 127) {
            ch.select = Select::None; // the null RPN: nothing selected until the next selection
        }
        message.flags |= kMidiNrpnPart;
        return std::nullopt;
    }
    case 6:
    case 38:
    case 96:
    case 97:
        break;
    default:
        return std::nullopt;
    }
    if (ch.select == Select::None) {
        return std::nullopt; // an ordinary CC 6/38/96/97
    }
    message.flags |= kMidiNrpnPart;
    const auto param = static_cast<std::uint16_t>((ch.msb << 7) | ch.lsb);
    if (ch.valueParam != param || ch.valueSelect != ch.select) {
        // A different parameter: its value and resolution start over.
        ch.valueParam = param;
        ch.valueSelect = ch.select;
        ch.value = 0;
        ch.hiRes = false;
    }
    switch (message.data1) {
    case 6:
        ch.value = static_cast<std::uint16_t>(v << 7);
        break;
    case 38:
        ch.hiRes = true;
        ch.value = static_cast<std::uint16_t>((ch.value & 0x3F80) | v);
        break;
    case 96:
    case 97: {
        const int step = ch.hiRes ? 1 : 128;
        const int top = ch.hiRes ? 16383 : (127 << 7);
        const int next = static_cast<int>(ch.value) + (message.data1 == 96 ? step : -step);
        ch.value = static_cast<std::uint16_t>(std::clamp(next, 0, top));
        break;
    }
    default:
        break;
    }
    MidiMessage out;
    out.kind = ch.select == Select::Nrpn ? MidiKind::Nrpn : MidiKind::Rpn;
    out.channel = message.channel;
    out.device = message.device;
    out.timestampNs = message.timestampNs;
    out.param = param;
    out.value14 = ch.value;
    out.flags = ch.hiRes ? kMidiHiRes : 0;
    return out;
}

} // namespace avgen::control
