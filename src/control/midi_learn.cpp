#include "control/midi_learn.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::control {

// ---- Learn ---------------------------------------------------------------------------------------------------------

void MidiLearn::arm() {
    state_ = State::Listening;
    updates_ = 0;
}

void MidiLearn::cancel() {
    state_ = State::Idle;
    updates_ = 0;
}

void MidiLearn::setFilter(int deviceSlot, int channel) {
    filterDevice_ = deviceSlot;
    filterChannel_ = channel;
}

bool MidiLearn::learnable(const MidiMessage& m) {
    switch (m.kind) {
    case MidiKind::ControlChange:
        if ((m.flags & kMidiNrpnPart) != 0) {
            return false; // a piece of an NRPN/RPN: the decoded message that follows is the source
        }
        if (m.data1 == 0 || m.data1 == 32 || m.data1 >= 120) {
            return false; // bank select (it precedes a program change), channel mode messages
        }
        return true;
    case MidiKind::NoteOn:
    case MidiKind::PolyPressure:
    case MidiKind::ProgramChange:
    case MidiKind::ChannelPressure:
    case MidiKind::PitchBend:
    case MidiKind::Nrpn:
    case MidiKind::Rpn:
        return true;
    default:
        return false; // note-off, clock, transport, system, device removals
    }
}

bool MidiLearn::sameSource(const MidiMessage& m) const {
    const auto family = [](MidiKind k) { return k == MidiKind::NoteOff ? MidiKind::NoteOn : k; };
    if (m.device != captured_.device || m.channel != captured_.channel || family(m.kind) != family(captured_.kind)) {
        return false;
    }
    switch (captured_.kind) {
    case MidiKind::Nrpn:
    case MidiKind::Rpn: return m.param == captured_.param;
    case MidiKind::PitchBend:
    case MidiKind::ChannelPressure: return true;
    default: return m.data1 == captured_.data1;
    }
}

bool MidiLearn::feed(const MidiMessage& m) {
    if (state_ == State::Listening) {
        if (!learnable(m)) {
            return false;
        }
        if (filterDevice_ >= 0 && m.device != filterDevice_) {
            return false;
        }
        if (filterChannel_ >= 0 && m.channel != filterChannel_) {
            return false;
        }
        captureFrom(m);
        return true;
    }
    if (state_ == State::Captured && sameSource(m)) {
        live_ = m.kind == MidiKind::NoteOff ? 0.0f : m.normalized();
        ++updates_;
    }
    return false;
}

void MidiLearn::captureFrom(const MidiMessage& m) {
    captured_ = m;
    live_ = m.normalized();
    updates_ = 0;
    state_ = State::Captured;
}

// ---- building and committing mappings ------------------------------------------------------------------------------

MidiBinding midiBindingFromMessage(const MidiMessage& m, const std::vector<MidiDeviceSlot>& devices, bool anyDevice,
                                   bool anyChannel, bool noteAsTrigger) {
    MidiBinding b;
    b.source = "*";
    b.channel = anyChannel ? -1 : m.channel;
    switch (m.kind) {
    case MidiKind::ControlChange: b.kind = MidiBindKind::ControlChange; b.number = m.data1; break;
    case MidiKind::NoteOn:
    case MidiKind::NoteOff:
        b.kind = noteAsTrigger ? MidiBindKind::NoteEvent : MidiBindKind::Note;
        b.number = m.data1;
        break;
    case MidiKind::PolyPressure: b.kind = MidiBindKind::PolyPressure; b.number = m.data1; break;
    case MidiKind::ProgramChange: b.kind = MidiBindKind::Program; b.number = m.data1; break;
    case MidiKind::ChannelPressure: b.kind = MidiBindKind::ChannelPressure; break;
    case MidiKind::PitchBend: b.kind = MidiBindKind::PitchBend; break;
    case MidiKind::Nrpn: b.kind = MidiBindKind::Nrpn; b.number = m.param; break;
    case MidiKind::Rpn: b.kind = MidiBindKind::Rpn; b.number = m.param; break;
    default: break;
    }
    if (!anyDevice && m.device < devices.size()) {
        b.device = devices[m.device].id;
        b.deviceName = devices[m.device].name;
    }
    return b;
}

std::vector<const MidiBinding*> midiCollisions(const ControlMap& map, std::string_view sceneKey,
                                               const MidiBinding& candidate) {
    std::vector<const MidiBinding*> out;
    for (const ResolvedMapping& r : resolveMidiMappings(map, sceneKey)) {
        if (r.state == MidiMappingState::Active && midiSourcesOverlap(*r.binding, candidate)) {
            out.push_back(r.binding);
        }
    }
    return out;
}

std::string commitMidiLearn(ControlMap& map, std::string_view sceneKey, MidiBinding candidate, MidiLearnCommit how) {
    if (how == MidiLearnCommit::Replace) {
        std::vector<std::string> ids;
        for (const MidiBinding* b : midiCollisions(map, sceneKey, candidate)) {
            ids.push_back(b->id);
        }
        for (const std::string& id : ids) {
            setMidiMappingEnabled(map, id, false);
        }
    }
    if (candidate.scope == MidiScope::Default) {
        candidate.scope = MidiScope::Scene;
    }
    if (candidate.scope == MidiScope::Scene) {
        candidate.scene = std::string(sceneKey);
    } else {
        candidate.scene.clear();
    }
    candidate.id = newMidiMappingId();
    candidate.idAuthored = false;
    candidate.enabled = true;
    std::string id = candidate.id;
    map.midiUser.push_back(std::move(candidate));
    return id;
}

namespace {

bool isDefault(const ControlMap& map, std::string_view id) {
    return std::any_of(map.midi.begin(), map.midi.end(), [&](const MidiBinding& b) { return b.id == id; });
}

} // namespace

MidiBinding* editableMidiMapping(ControlMap& map, std::string_view sceneKey, std::string_view id) {
    for (auto& b : map.midiUser) {
        if (b.id == id) {
            return &b;
        }
    }
    const auto def = std::find_if(map.midi.begin(), map.midi.end(), [&](const MidiBinding& b) { return b.id == id; });
    if (def == map.midi.end()) {
        return nullptr;
    }
    for (auto& b : map.midiUser) {
        if (b.overrides == id && b.scope == MidiScope::Scene && b.scene == sceneKey) {
            return &b;
        }
    }
    // The shipped default is never written: the edit goes to a scene mapping that overrides it.
    MidiBinding copy = *def;
    copy.id = newMidiMappingId();
    copy.idAuthored = false;
    copy.scope = MidiScope::Scene;
    copy.scene = std::string(sceneKey);
    copy.overrides = std::string(id);
    copy.enabled = true;
    map.midiUser.push_back(std::move(copy));
    return &map.midiUser.back();
}

std::string duplicateMidiMapping(ControlMap& map, std::string_view sceneKey, std::string_view id) {
    const MidiBinding* original = map.findMidi(id);
    if (original == nullptr || original->invalid()) {
        return {};
    }
    MidiBinding copy = *original;
    copy.id = newMidiMappingId();
    copy.idAuthored = false;
    copy.overrides.clear();
    copy.enabled = true;
    if (copy.scope == MidiScope::Default) {
        copy.scope = MidiScope::Scene;
        copy.scene = std::string(sceneKey);
    }
    std::string newId = copy.id;
    map.midiUser.push_back(std::move(copy));
    return newId;
}

bool removeMidiMapping(ControlMap& map, std::string_view id) {
    const auto before = map.midiUser.size();
    std::erase_if(map.midiUser, [&](const MidiBinding& b) { return b.id == id; });
    return map.midiUser.size() != before;
}

void setMidiMappingEnabled(ControlMap& map, std::string_view id, bool enabled) {
    if (isDefault(map, id)) {
        auto& off = map.disabledDefaults;
        std::erase(off, std::string(id));
        if (!enabled) {
            off.emplace_back(id);
        }
        return;
    }
    for (auto& b : map.midiUser) {
        if (b.id == id && !b.invalid()) {
            b.enabled = enabled;
        }
    }
}

bool midiMappingEnabled(const ControlMap& map, std::string_view id) {
    if (isDefault(map, id)) {
        return std::find(map.disabledDefaults.begin(), map.disabledDefaults.end(), id) == map.disabledDefaults.end();
    }
    const MidiBinding* b = map.findMidi(id);
    return b != nullptr && b->enabled;
}

void restoreMidiDefault(ControlMap& map, std::string_view id) {
    std::erase(map.disabledDefaults, std::string(id));
    std::erase_if(map.midiUser, [&](const MidiBinding& b) { return b.overrides == id; });
}

void resetSceneMidiMappings(ControlMap& map, std::string_view sceneKey) {
    std::erase_if(map.midiUser, [&](const MidiBinding& b) { return b.scope == MidiScope::Scene && b.scene == sceneKey; });
    map.disabledDefaults.clear();
}

bool promoteMidiMappingToDefault(ControlMap& map, std::string_view id) {
    const auto it = std::find_if(map.midiUser.begin(), map.midiUser.end(), [&](const MidiBinding& b) { return b.id == id; });
    if (it == map.midiUser.end() || it->invalid()) {
        return false;
    }
    MidiBinding b = *it;
    map.midiUser.erase(it);
    b.scope = MidiScope::Default;
    b.scene.clear();
    b.enabled = true;
    if (!b.overrides.empty()) {
        // Promoting an edit of a default replaces that default, under the default's own id.
        for (auto& d : map.midi) {
            if (d.id == b.overrides) {
                const std::string keepId = d.id;
                const bool keepAuthored = d.idAuthored;
                b.overrides.clear();
                d = std::move(b);
                d.id = keepId;
                d.idAuthored = keepAuthored;
                std::erase(map.disabledDefaults, keepId);
                return true;
            }
        }
        b.overrides.clear();
    }
    b.idAuthored = true;
    map.midi.push_back(std::move(b));
    return true;
}

// ---- monitor -------------------------------------------------------------------------------------------------------

void MidiMonitor::push(const MidiMessage& m, double seconds, std::uint16_t matched, bool ignored) {
    if (!showClock_ && (m.kind == MidiKind::Clock || m.kind == MidiKind::Other)) {
        return; // 24 per beat and active sensing would bury everything else; the device row counts them instead
    }
    ++total_;
    if (coalesce_ && size_ > 0 && m.continuous()) {
        Row& last = rows_[(head_ + size_ - 1) % kCapacity];
        const MidiMessage& p = last.message;
        const bool same = p.kind == m.kind && p.device == m.device && p.channel == m.channel &&
                          (m.kind == MidiKind::Nrpn || m.kind == MidiKind::Rpn ? p.param == m.param : p.data1 == m.data1);
        if (same && seconds - last.seconds < 1.0) {
            last.message = m;
            last.seconds = seconds;
            ++last.repeats;
            last.matched = std::max(last.matched, matched);
            return;
        }
    }
    Row& row = rows_[(head_ + size_) % kCapacity];
    if (size_ < kCapacity) {
        ++size_;
    } else {
        head_ = (head_ + 1) % kCapacity; // full: the oldest row goes
    }
    row = Row{seconds, m, 1, matched, ignored};
}

void MidiMonitor::clear() {
    head_ = 0;
    size_ = 0;
}

std::string midiMessageDescription(const MidiMessage& m) {
    switch (m.kind) {
    case MidiKind::ControlChange: return fmt::format("CC {}  {}/127", m.data1, m.data2);
    case MidiKind::NoteOn: return fmt::format("Note On {}  vel {}", midiNoteName(m.data1), m.data2);
    case MidiKind::NoteOff: return fmt::format("Note Off {}", midiNoteName(m.data1));
    case MidiKind::PolyPressure: return fmt::format("Poly pressure {}  {}/127", midiNoteName(m.data1), m.data2);
    case MidiKind::ProgramChange: return fmt::format("Program {}", m.data1 + 1);
    case MidiKind::ChannelPressure: return fmt::format("Channel pressure  {}/127", m.data2);
    case MidiKind::PitchBend: {
        const int percent = static_cast<int>(std::lround((m.normalized() - 0.5f) * 200.0f));
        return percent == 0 ? std::string("Pitch bend  centre") : fmt::format("Pitch bend  centre {:+d}%", percent);
    }
    case MidiKind::Nrpn:
    case MidiKind::Rpn: {
        const char* kind = m.kind == MidiKind::Nrpn ? "NRPN" : "RPN";
        std::string name;
        if (m.kind == MidiKind::Rpn) {
            // The registered parameters every device shares (MIDI 1.0 spec); anything else is the device's own.
            switch (m.param) {
            case 0: name = " (pitch bend range)"; break;
            case 1: name = " (fine tuning)"; break;
            case 2: name = " (coarse tuning)"; break;
            default: break;
            }
        }
        if ((m.flags & kMidiHiRes) != 0) {
            return fmt::format("{} {}{}  {}/16383", kind, m.param, name, m.value14);
        }
        return fmt::format("{} {}{}  {}/127", kind, m.param, name, m.value14 >> 7);
    }
    case MidiKind::Clock: return "Clock";
    case MidiKind::Start: return "Start";
    case MidiKind::Continue: return "Continue";
    case MidiKind::Stop: return "Stop";
    case MidiKind::DeviceRemoved: return "Device removed";
    default: break;
    }
    return fmt::format("System 0x{:02X}", m.data1);
}

} // namespace avgen::control
