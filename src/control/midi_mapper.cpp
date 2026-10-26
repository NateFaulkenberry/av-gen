#include "control/midi_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace avgen::control {

namespace {

bool anyDeviceBinding(const MidiBinding& b) {
    return b.device.empty() && (b.source.empty() || b.source == "*");
}

std::string primaryKey(const BindingTarget& t) {
    const auto keys = midiDestinationKeys(t);
    return keys.empty() ? std::string() : keys.back(); // the parameter when there is one, else the channel
}

} // namespace

const char* midiDeviceStateName(MidiDeviceState state) {
    switch (state) {
    case MidiDeviceState::Any: return "any device";
    case MidiDeviceState::Connected: return "connected";
    case MidiDeviceState::ByName: return "matched by name";
    case MidiDeviceState::Absent: return "device not connected";
    }
    return "any device";
}

void MidiMapper::rebuild(const ControlMap& map, std::string_view sceneKey, MidiTargets& targets, bool keepState) {
    const auto resolved = resolveMidiMappings(map, sceneKey);
    if (!keepState) {
        releaseAll(targets);
        for (Entry& e : entries_) {
            e.rt = Runtime{};
        }
    }
    // A gate whose mapping is leaving is released first (while its status row is still its own), so leaving a scene or
    // disabling a mapping never strands a held note.
    for (Entry& old : entries_) {
        if (old.rt.heldCount == 0 && !old.rt.heldOverflow) {
            continue;
        }
        const bool stays = std::any_of(resolved.begin(), resolved.end(), [&](const ResolvedMapping& r) {
            return r.state == MidiMappingState::Active && r.binding->id == old.binding.id;
        });
        if (!stays) {
            releaseGate(old, targets);
        }
    }

    std::unordered_map<std::string, Runtime> kept;
    if (keepState) {
        for (const Entry& e : entries_) {
            kept.emplace(e.binding.id, e.rt);
        }
    }
    entries_.clear();
    statuses_.clear();
    statusIndex_.clear();

    // Applied in precedence order -- default, rig, project, scene -- so on one message the higher scope writes last.
    for (const MidiScope scope : {MidiScope::Default, MidiScope::Rig, MidiScope::Project, MidiScope::Scene}) {
        for (const ResolvedMapping& r : resolved) {
            const MidiBinding& b = *r.binding;
            if (b.scope != scope) {
                continue;
            }
            MidiMappingStatus status;
            status.state = r.state;
            status.by = r.by;
            if (!b.target.parameter.empty()) {
                status.destinationMissing = !targets.parameter(b.target.parameter, b.target.component).has_value();
            }
            // ADR-1705: a rig mapping whose parameter this scene lacks is inactive here -- kept, shown, not applied.
            status.inactiveForScene = b.scope == MidiScope::Rig && status.destinationMissing;
            const bool inactive = status.inactiveForScene;
            const std::size_t statusAt = statuses_.size();
            statuses_.push_back(std::move(status));
            if (!b.id.empty()) {
                statusIndex_[b.id] = statusAt;
            }
            if (r.state != MidiMappingState::Active || inactive || entries_.size() >= 0xFFFF) {
                continue;
            }
            Entry e;
            e.binding = b;
            e.status = statusAt;
            if (const auto it = kept.find(b.id); it != kept.end()) {
                e.rt = it->second;
            }
            const ChannelRest* rest = b.target.signal.empty() ? nullptr : map.restFor(b.target.signal);
            e.effectivePickup = midiBindKindIsContinuous(b.kind) && b.encoder == MidiEncoder::Absolute &&
                                (b.pickup || map.midiPickup || (rest != nullptr && rest->pickup));
            e.combine = map.combineFor(primaryKey(b.target));
            entries_.push_back(std::move(e));
        }
    }
    // Combination groups: the entries that share a max/add destination.
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        Entry& e = entries_[i];
        e.combineGroup.clear();
        if (e.combine == MidiCombine::Replace) {
            continue;
        }
        const std::string key = primaryKey(e.binding.target);
        for (std::size_t j = 0; j < entries_.size(); ++j) {
            if (primaryKey(entries_[j].binding.target) == key) {
                e.combineGroup.push_back(static_cast<std::uint16_t>(j));
            }
        }
    }
    for (auto* list : {&byCc_, &byNote_, &byPoly_, &byProgram_}) {
        for (auto& bucket : *list) {
            bucket.clear();
        }
    }
    for (auto* list : {&anyCc_, &anyNote_, &anyPoly_, &anyProgram_, &pitchBend_, &pressure_, &anyNrpn_, &anyRpn_}) {
        list->clear();
    }
    byNrpn_.clear();
    byRpn_.clear();
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        indexEntry(static_cast<std::uint16_t>(i));
        resolveDevices(entries_[i]);
    }
    scratch_.reserve(entries_.size() + 1);
}

void MidiMapper::indexEntry(std::uint16_t index) {
    const MidiBinding& b = entries_[index].binding;
    const int lo = b.number;
    const int hi = std::min(127, b.numberHigh >= b.number ? b.numberHigh : b.number);
    const auto range = [&](std::array<std::vector<std::uint16_t>, 128>& table, std::vector<std::uint16_t>& any) {
        if (lo < 0) {
            any.push_back(index);
            return;
        }
        for (int n = lo; n <= hi; ++n) {
            table[static_cast<std::size_t>(n)].push_back(index);
        }
    };
    switch (b.kind) {
    case MidiBindKind::ControlChange:
        if (lo < 0) {
            anyCc_.push_back(index);
        } else if (lo < 128) {
            byCc_[static_cast<std::size_t>(lo)].push_back(index);
        }
        break;
    case MidiBindKind::Note:
    case MidiBindKind::NoteEvent: range(byNote_, anyNote_); break;
    case MidiBindKind::PolyPressure: range(byPoly_, anyPoly_); break;
    case MidiBindKind::Program:
        if (lo < 0) {
            anyProgram_.push_back(index);
        } else if (lo < 128) {
            byProgram_[static_cast<std::size_t>(lo)].push_back(index);
        }
        break;
    case MidiBindKind::PitchBend: pitchBend_.push_back(index); break;
    case MidiBindKind::ChannelPressure: pressure_.push_back(index); break;
    case MidiBindKind::Nrpn:
        if (lo < 0) {
            anyNrpn_.push_back(index);
        } else {
            byNrpn_[static_cast<std::uint16_t>(lo)].push_back(index);
        }
        break;
    case MidiBindKind::Rpn:
        if (lo < 0) {
            anyRpn_.push_back(index);
        } else {
            byRpn_[static_cast<std::uint16_t>(lo)].push_back(index);
        }
        break;
    }
}

void MidiMapper::setDevices(const std::vector<MidiDeviceSlot>& devices) {
    devices_ = devices;
    for (Entry& e : entries_) {
        resolveDevices(e);
    }
}

void MidiMapper::resolveDevices(Entry& e) {
    const MidiBinding& b = e.binding;
    e.deviceMatches.assign(devices_.size(), false);
    MidiDeviceState state = MidiDeviceState::Any;
    if (b.device.empty()) {
        bool connectedMatch = false;
        for (std::size_t i = 0; i < devices_.size(); ++i) {
            e.deviceMatches[i] = midiDeviceMatches(b, devices_[i].id, devices_[i].name);
            connectedMatch = connectedMatch || (e.deviceMatches[i] && devices_[i].connected);
        }
        state = anyDeviceBinding(b) ? MidiDeviceState::Any
                                    : (connectedMatch ? MidiDeviceState::Connected : MidiDeviceState::Absent);
    } else {
        bool present = false;
        for (std::size_t i = 0; i < devices_.size(); ++i) {
            if (devices_[i].id == b.device) {
                e.deviceMatches[i] = true;
                present = present || devices_[i].connected;
            }
        }
        bool byName = false;
        if (!present && !b.deviceName.empty()) {
            // Another computer, or the device re-enumerated with a new id: the same name is taken to be the same
            // instrument, and the panel says so.
            for (std::size_t i = 0; i < devices_.size(); ++i) {
                if (devices_[i].connected && devices_[i].name == b.deviceName) {
                    e.deviceMatches[i] = true;
                    byName = true;
                }
            }
        }
        state = present ? MidiDeviceState::Connected : (byName ? MidiDeviceState::ByName : MidiDeviceState::Absent);
    }
    statuses_[e.status].device = state;
}

bool MidiMapper::deviceOk(const Entry& e, std::uint16_t device) const {
    if (device >= e.deviceMatches.size()) {
        return anyDeviceBinding(e.binding); // a message with no device table entry (built by hand)
    }
    return e.deviceMatches[device];
}

float MidiMapper::shape(const Entry& e, float x) const {
    const MidiBinding& b = e.binding;
    x = std::clamp(x, 0.0f, 1.0f);
    if (b.invert) {
        x = 1.0f - x;
    }
    x = applyMidiCurve(b.curve, x);
    return std::clamp(x * b.scale + b.offset, 0.0f, 1.0f);
}

float MidiMapper::current01(const Entry& e, MidiTargets& targets) const {
    const BindingTarget& t = e.binding.target;
    const float span = t.max - t.min;
    if (!t.parameter.empty()) {
        if (const auto base = targets.parameter(t.parameter, t.component); base && span != 0.0f) {
            return std::clamp((*base - t.min) / span, 0.0f, 1.0f);
        }
    }
    // A control channel only moves with its knob (or OSC); with a declared rest (ADR-1612) there is a value to take
    // over from -- the rest, until the knob reaches it.
    if (!t.signal.empty() && targets.signalHasRest(t.signal) && span != 0.0f) {
        return std::clamp((targets.signal(t.signal) - t.min) / span, 0.0f, 1.0f);
    }
    return -1.0f;
}

void MidiMapper::write(Entry& e, float y, MidiTargets& targets, double nowSeconds) {
    const BindingTarget& t = e.binding.target;
    e.rt.contribution = y;
    e.rt.contributed = true;
    if (e.effectivePickup) {
        e.rt.pickup.lastSent = y; // what this mapping last applied (smoothing writes between messages)
    }
    // The destination's value in its units. Max and Add fold every contributing mapping of the group, each through its
    // own range, with the route combiner's arithmetic.
    float units = t.min + (t.max - t.min) * y;
    if (e.combine != MidiCombine::Replace && !e.combineGroup.empty()) {
        bool first = true;
        for (const std::uint16_t g : e.combineGroup) {
            const Entry& other = entries_[g];
            if (!other.rt.contributed) {
                continue;
            }
            const BindingTarget& ot = other.binding.target;
            const float v = ot.min + (ot.max - ot.min) * other.rt.contribution;
            if (first) {
                units = v;
                first = false;
            } else {
                units = e.combine == MidiCombine::Max ? std::max(units, v) : units + v;
            }
        }
    }
    if (!t.signal.empty()) {
        targets.setSignal(t.signal, std::clamp(units, 0.0f, 1.0f));
    }
    if (!t.parameter.empty()) {
        targets.setParameter(t.parameter, t.component, units);
    }
    MidiMappingStatus& s = statuses_[e.status];
    s.lastOutput = units;
    s.lastSeconds = nowSeconds;
    s.held = e.rt.heldCount;
}

void MidiMapper::pulse(Entry& e, float y, MidiTargets& targets, double nowSeconds) {
    const BindingTarget& t = e.binding.target;
    const float units = t.min + (t.max - t.min) * y;
    if (!t.signal.empty()) {
        targets.pulseSignal(t.signal, std::clamp(units, 0.0f, 1.0f));
    }
    if (!t.parameter.empty()) {
        targets.setParameter(t.parameter, t.component, units); // a trigger on a parameter sets it, as it always did
    }
    MidiMappingStatus& s = statuses_[e.status];
    s.lastOutput = units;
    s.lastSeconds = nowSeconds;
}

void MidiMapper::releaseGate(Entry& e, MidiTargets& targets) {
    e.rt.heldCount = 0;
    e.rt.heldOverflow = false;
    write(e, shape(e, 0.0f), targets, statuses_[e.status].lastSeconds);
}

void MidiMapper::releaseAll(MidiTargets& targets) {
    for (Entry& e : entries_) {
        if (e.rt.heldCount > 0 || e.rt.heldOverflow) {
            releaseGate(e, targets);
        }
    }
}

void MidiMapper::resetPickup() {
    for (Entry& e : entries_) {
        e.rt.pickup = PickupState{};
    }
}

const MidiMappingStatus* MidiMapper::status(std::string_view id) const {
    const auto it = statusIndex_.find(std::string(id));
    return it == statusIndex_.end() ? nullptr : &statuses_[it->second];
}

void MidiMapper::collect(const MidiMessage& m) {
    scratch_.clear();
    // Every index list is built in entry order, and entries are in precedence order (default, rig, project, scene), so
    // a message's candidates -- one numbered list and one "any" list, disjoint -- are merged in order: no sort, and no
    // allocation (scratch_ holds every entry's index).
    static const std::vector<std::uint16_t> kNone;
    const auto merge = [&](const std::vector<std::uint16_t>& a, const std::vector<std::uint16_t>& b) {
        std::merge(a.begin(), a.end(), b.begin(), b.end(), std::back_inserter(scratch_));
    };
    const auto mapped = [&](const std::unordered_map<std::uint16_t, std::vector<std::uint16_t>>& table)
        -> const std::vector<std::uint16_t>& {
        const auto it = table.find(m.param);
        return it != table.end() ? it->second : kNone;
    };
    switch (m.kind) {
    case MidiKind::ControlChange: merge(byCc_[m.data1 & 0x7F], anyCc_); break;
    case MidiKind::NoteOn:
    case MidiKind::NoteOff: merge(byNote_[m.data1 & 0x7F], anyNote_); break;
    case MidiKind::PolyPressure: merge(byPoly_[m.data1 & 0x7F], anyPoly_); break;
    case MidiKind::ProgramChange: merge(byProgram_[m.data1 & 0x7F], anyProgram_); break;
    case MidiKind::PitchBend: merge(pitchBend_, kNone); break;
    case MidiKind::ChannelPressure: merge(pressure_, kNone); break;
    case MidiKind::Nrpn: merge(mapped(byNrpn_), anyNrpn_); break;
    case MidiKind::Rpn: merge(mapped(byRpn_), anyRpn_); break;
    default: break;
    }
}

// ADR-1705: an action fires once on the source's rising edge: a note-on, a program change, a continuous control
// crossing half-way upwards (a button sending 127 then 0 is one press).
void MidiMapper::fireAction(Entry& e, bool high, MidiTargets& targets, double nowSeconds) {
    const bool rising = high && !e.rt.high;
    e.rt.high = high;
    if (!rising) {
        return;
    }
    targets.action(e.binding.target.action);
    MidiMappingStatus& s = statuses_[e.status];
    s.lastOutput = 1.0f;
    s.lastSeconds = nowSeconds;
}

int MidiMapper::apply(const MidiMessage& m, MidiTargets& targets, double nowSeconds) {
    if (m.kind == MidiKind::DeviceRemoved) {
        // Its note-offs will never come: whatever it holds is released (kAllMidiDevices: notes were lost in a full
        // queue, so everything is).
        for (Entry& e : entries_) {
            if (e.rt.heldCount == 0 && !e.rt.heldOverflow) {
                continue;
            }
            if (m.device == kAllMidiDevices) {
                releaseGate(e, targets);
                continue;
            }
            std::uint8_t kept = 0;
            for (std::uint8_t i = 0; i < e.rt.heldCount; ++i) {
                if (e.rt.held[i].device != m.device) {
                    e.rt.held[kept++] = e.rt.held[i];
                }
            }
            e.rt.heldCount = kept;
            if (kept == 0) {
                releaseGate(e, targets);
            }
            statuses_[e.status].held = e.rt.heldCount;
        }
        return 0;
    }
    collect(m);
    int moved = 0;
    for (const std::uint16_t index : scratch_) {
        Entry& e = entries_[index];
        if (!deviceOk(e, m.device)) {
            continue;
        }
        const auto raw = midiSourceValue(e.binding, m);
        if (!raw) {
            continue;
        }
        const MidiBinding& b = e.binding;
        MidiMappingStatus& s = statuses_[e.status];
        s.lastInput = *raw;
        s.lastSeconds = nowSeconds;
        if (b.target.action != MidiAction::None) {
            bool high = false;
            if (b.kind == MidiBindKind::Note || b.kind == MidiBindKind::NoteEvent) {
                high = m.kind == MidiKind::NoteOn;
            } else if (b.kind == MidiBindKind::Program) {
                e.rt.high = false; // every program change is a press
                high = true;
            } else {
                high = shape(e, *raw) >= 0.5f;
            }
            fireAction(e, high, targets, nowSeconds);
            ++moved;
            continue;
        }
        switch (b.kind) {
        case MidiBindKind::Note: {
            if (b.toggle) {
                if (m.kind == MidiKind::NoteOn) {
                    e.rt.toggle = !e.rt.toggle;
                    write(e, shape(e, e.rt.toggle ? 1.0f : 0.0f), targets, nowSeconds);
                    ++moved;
                }
                break;
            }
            const auto same = [&](const Held& h) { return h.device == m.device && h.channel == m.channel && h.note == m.data1; };
            if (m.kind == MidiKind::NoteOn) {
                const bool already = std::any_of(e.rt.held.begin(), e.rt.held.begin() + e.rt.heldCount, same);
                if (!already) {
                    if (e.rt.heldCount < kMaxHeld) {
                        e.rt.held[e.rt.heldCount++] = Held{m.device, m.channel, m.data1};
                    } else {
                        e.rt.heldOverflow = true;
                    }
                }
                write(e, shape(e, b.velocity ? *raw : 1.0f), targets, nowSeconds);
            } else {
                std::uint8_t kept = 0;
                for (std::uint8_t i = 0; i < e.rt.heldCount; ++i) {
                    if (!same(e.rt.held[i])) {
                        e.rt.held[kept++] = e.rt.held[i];
                    }
                }
                e.rt.heldCount = kept;
                if (kept == 0) {
                    // Every note of the range is up: the gate closes (a velocity-0 note-on arrives here as a NoteOff).
                    e.rt.heldOverflow = false;
                    write(e, shape(e, 0.0f), targets, nowSeconds);
                }
                s.held = e.rt.heldCount;
            }
            ++moved;
            break;
        }
        case MidiBindKind::NoteEvent:
            pulse(e, shape(e, b.velocity ? *raw : 1.0f), targets, nowSeconds);
            ++moved;
            break;
        case MidiBindKind::Program:
            if (b.number >= 0) {
                pulse(e, shape(e, 1.0f), targets, nowSeconds);
            } else {
                write(e, shape(e, *raw), targets, nowSeconds);
            }
            ++moved;
            break;
        default: { // the continuous kinds
            float y = 0.0f;
            const bool relative = b.kind == MidiBindKind::ControlChange && b.encoder != MidiEncoder::Absolute;
            if (relative) {
                int delta = relativeEncoderDelta(b.encoder, m.data2);
                if (b.invert) {
                    delta = -delta;
                }
                if (e.rt.relative < 0.0f) {
                    // Starts where the destination is, so the first detent moves it one step rather than jumping.
                    const float c = current01(e, targets);
                    const BindingTarget& t = b.target;
                    const float fromSignal = !t.signal.empty() && t.max != t.min
                                                 ? std::clamp((targets.signal(t.signal) - t.min) / (t.max - t.min), 0.0f, 1.0f)
                                                 : 0.0f;
                    e.rt.relative = c >= 0.0f ? c : fromSignal;
                }
                e.rt.relative = std::clamp(e.rt.relative + static_cast<float>(delta) * b.scale / 127.0f, 0.0f, 1.0f);
                y = e.rt.relative;
            } else {
                y = shape(e, *raw);
            }
            if (e.effectivePickup && e.combine == MidiCombine::Replace) {
                const float c = current01(e, targets);
                if (c >= 0.0f && !pickupAccepts(e.rt.pickup, y, c)) {
                    ++pickupHeld_;
                    s.waitingForPickup = true;
                    break;
                }
                s.waitingForPickup = false;
            }
            if (b.smoothMs > 0.0f) {
                e.rt.smoothTarget = y;
                if (!e.rt.smoothPrimed) {
                    const float c = current01(e, targets);
                    e.rt.smoothValue = c >= 0.0f ? c : y;
                    e.rt.smoothPrimed = true;
                }
                e.rt.smoothing = true; // advance() writes it, this frame
            } else {
                write(e, y, targets, nowSeconds);
            }
            ++moved;
            break;
        }
        }
    }
    return moved;
}

void MidiMapper::advance(double dtSeconds, MidiTargets& targets) {
    for (Entry& e : entries_) {
        if (!e.rt.smoothing) {
            continue;
        }
        const double tau = static_cast<double>(e.binding.smoothMs) / 1000.0;
        const float a = tau > 0.0 ? static_cast<float>(1.0 - std::exp(-std::max(0.0, dtSeconds) / tau)) : 1.0f;
        e.rt.smoothValue += (e.rt.smoothTarget - e.rt.smoothValue) * a;
        if (std::abs(e.rt.smoothTarget - e.rt.smoothValue) < 1e-4f) {
            e.rt.smoothValue = e.rt.smoothTarget;
            e.rt.smoothing = false;
        }
        write(e, e.rt.smoothValue, targets, statuses_[e.status].lastSeconds);
    }
}

} // namespace avgen::control
