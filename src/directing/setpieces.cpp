#include "directing/setpieces.hpp"

#include "directing/text.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>

namespace avgen::directing {
namespace {

// How close the canopy may come to a hovering craft's hull before the craft is hovering in it. The
// saucer's hull is about seven metres deep; two metres is the margin, not the hull.
constexpr float kCanopyMargin = 2.0f;

Issue& add(std::vector<Issue>& issues, Severity severity, IssueCode code, const std::string& item, std::string location,
           std::string message) {
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.item = item;
    issue.location = std::move(location);
    issue.message = std::move(message);
    issues.push_back(std::move(issue));
    return issues.back();
}

std::string xz(glm::vec2 p) { return fmt::format("({:.1f}, {:.1f})", p.x, p.y); }

float slot(const stage::SetPieceSpec& spec, std::string_view name) { return stage::setPieceValue(spec, name); }

// This plan's previous revision's scenarios: a name it made is its own to replace, not a collision.
bool producedBefore(const Plan& plan, const SceneFacts& facts, std::string_view scenario) {
    if (const Plan* previous = facts.plan(plan.id); previous != nullptr) {
        for (const ContentRef& ref : previous->produced) {
            if (ref.domain == ContentDomain::StagingScenario && ref.id == scenario) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

ResolvedSetPiece resolveSetPiece(const Plan& plan, std::size_t index, const SceneFacts& facts, const PlanTimes& times,
                                 std::vector<Issue>& issues) {
    const PlanSetPiece& sp = plan.setPieces[index];
    const std::string at = fmt::format("/setPieces/{}", index);
    ResolvedSetPiece out;
    out.key = sp.key;
    out.plan = plan.id;
    out.framingMetres = sp.framingMetres;
    const auto error = [&](IssueCode code, std::string location, std::string message) -> Issue& {
        return add(issues, Severity::Error, code, sp.key, std::move(location), std::move(message));
    };
    const auto warning = [&](IssueCode code, std::string location, std::string message) -> Issue& {
        return add(issues, Severity::Warning, code, sp.key, std::move(location), std::move(message));
    };

    // ---- the template ------------------------------------------------------------------------------
    const auto kind = stage::setPieceKindFromName(sp.templateName);
    if (!kind) {
        Issue& issue = error(IssueCode::UnknownSubject, at + "/template",
                             fmt::format("'{}' is not a set piece template", sp.templateName));
        issue.suggestions = text::nearest(sp.templateName, stage::setPieceKindNames());
        if (issue.suggestions.empty()) {
            issue.suggestions = stage::setPieceKindNames();
        }
        issue.details = {{"templates", stage::setPieceKindNames()}};
        return out;
    }

    // ---- the craft: one of the scene's staging actors ------------------------------------------------
    const stage::StagingDesc& staging = facts.staged.staging;
    const auto craft = std::find_if(staging.actors.begin(), staging.actors.end(),
                                    [&](const stage::ActorDesc& a) { return a.name == sp.craft; });
    if (craft == staging.actors.end()) {
        std::vector<std::string> names;
        for (const stage::ActorDesc& a : staging.actors) {
            names.push_back(a.name);
        }
        Issue& issue = error(IssueCode::UnknownSubject, at + "/craft",
                             names.empty() ? std::string("this scene's staging declares no actors, so nothing can play a "
                                                         "set piece")
                                           : fmt::format("there is no staging actor '{}' to play it", sp.craft));
        issue.suggestions = names.empty() ? std::vector<std::string>{"declare the craft in the scene's staging.actors, "
                                                                     "with its beam as a part"}
                                          : text::nearest(sp.craft, names);
        issue.details = {{"actors", names}};
        return out;
    }
    const auto beamPart = std::find_if(craft->parts.begin(), craft->parts.end(),
                                       [](const stage::ActorPart& p) { return p.name == "beam"; });
    if (*kind != stage::SetPieceKind::Flyby && beamPart == craft->parts.end()) {
        Issue& issue = error(IssueCode::CapabilityUnavailable, at + "/craft",
                             fmt::format("a {} lights the craft's beam, and '{}' has no part called 'beam'",
                                         sp.templateName, craft->name));
        issue.suggestions = {"add the beam entity to the actor's parts as {\"name\": \"beam\", ...}", "use a flyby"};
        return out;
    }

    // ---- when ------------------------------------------------------------------------------------------
    const auto t = times.at(at + "/at");
    if (!t) {
        return out; // unplaceable: the time's own issue says why, and blocks the item
    }

    // ---- where -----------------------------------------------------------------------------------------
    glm::vec2 point(0.0f);
    if (sp.where.point) {
        point = glm::vec2(sp.where.point->first, sp.where.point->second);
    } else {
        const Subject* subject = plan.subject(sp.where.near);
        if (subject == nullptr || subject->kind == SubjectKind::Unresolved) {
            return out; // unresolved: reported, and blocks this item
        }
        const Place* place = facts.place(subject->id);
        if (place == nullptr) {
            Issue& issue = error(IssueCode::SpatialInfeasible, at + "/place/near",
                                 fmt::format("'{}' has no place a craft can go to: a character is wherever its "
                                             "simulation has taken it, which is not a plan-time fact (ADR-758)",
                                             subject->id));
            issue.suggestions = {"place it near a hero or a node", "give it a point [x, z]"};
            return out;
        }
        point = glm::vec2(place->position.x, place->position.z);
    }
    point += glm::vec2(sp.where.offset.first, sp.where.offset.second);

    std::vector<std::string> animals;
    for (std::size_t a = 0; a < sp.animals.size(); ++a) {
        const Subject* subject = plan.subject(sp.animals[a]);
        if (subject == nullptr || subject->kind == SubjectKind::Unresolved) {
            return out;
        }
        if (subject->kind != SubjectKind::Entity) {
            error(IssueCode::CapabilityUnavailable, fmt::format("{}/animals/{}", at, a),
                  fmt::format("'{}' is a {}, and only an entity can be lifted", subject->id, subjectKindName(subject->kind)));
            return out;
        }
        animals.push_back(subject->id);
    }

    // ---- the spec ---------------------------------------------------------------------------------------
    stage::SetPieceSpec spec;
    spec.id = sp.key;
    spec.kind = *kind;
    spec.actor = craft->name;
    spec.moment = sp.moment;
    spec.atSeconds = *t;
    spec.place.kind = sp.where.region ? stage::SetPiecePlace::Kind::Region : stage::SetPiecePlace::Kind::Point;
    spec.place.point = point;
    spec.place.radius = sp.where.radius;
    spec.tag = sp.tag.empty() ? std::string("animal") : sp.tag;
    spec.overrides = sp.set;
    if (sp.beamColor) {
        spec.beamColor = glm::vec3((*sp.beamColor)[0], (*sp.beamColor)[1], (*sp.beamColor)[2]);
    }
    spec.animals = animals;
    spec.beamPart = beamPart != craft->parts.end();
    spec.frameSeconds = facts.frameSeconds;
    spec.groundAtPlace = facts.groundAt ? facts.groundAt(point.x, point.y) : 0.0f;
    if (spec.beamPart) {
        // The beam's authored colours, so a coloured set piece gives the next one the beam the scene
        // authored. Its node is the one its entity drives.
        std::string node = beamPart->entity;
        if (const CharacterMark* mark = facts.character(beamPart->entity); mark != nullptr && !mark->node.empty()) {
            node = mark->node;
        }
        const auto rest = [&](const char* channel) {
            glm::vec4 out4(0.0f);
            if (const std::vector<float>* base = facts.base(fmt::format("particles/{}/{}", node, channel));
                base != nullptr) {
                for (std::size_t c = 0; c < 4 && c < base->size(); ++c) {
                    out4[static_cast<int>(c)] = (*base)[c];
                }
            }
            return out4;
        };
        spec.beamRestStart = rest("colorStart");
        spec.beamRestEnd = rest("colorEnd");
    }
    if (auto ok = stage::validateSetPieceSpec(spec); !ok) {
        Issue& issue = error(IssueCode::SchemaInvalid, at, ok.error().message);
        for (const auto& [name, value] : sp.set) {
            if (stage::findSetPieceSlot(*kind, name) == nullptr) {
                issue.location = at + "/set/" + name;
                issue.suggestions = text::nearest(name, stage::setPieceSlotNames(*kind));
                issue.details = {{"slots", stage::setPieceSlotNames(*kind)}};
            }
        }
        return out;
    }
    auto timeline = stage::setPieceTimeline(spec);
    if (!timeline) {
        error(IssueCode::SchemaInvalid, at, timeline.error().message);
        return out;
    }

    // ---- inside the song ----------------------------------------------------------------------------------
    const std::string moment = spec.moment.empty() ? stage::defaultSetPieceMoment(spec.kind) : spec.moment;
    if (timeline->start < 0.0) {
        Issue& issue = error(IssueCode::TimeOutOfRange, at + "/at",
                             fmt::format("placing its {} at {:.3f}s needs the craft from {:.3f}s, before the film starts",
                                         moment, *t, timeline->start));
        issue.suggestions = {"place it later", "shorten its approach (set approachSeconds)"};
    }
    if (facts.music.durationSeconds > 0.0 && timeline->end > facts.music.durationSeconds + 1e-6) {
        Issue& issue = error(IssueCode::TimeOutOfRange, at + "/at",
                             fmt::format("it ends at {:.3f}s, after the piece ({:.3f}s)", timeline->end,
                                         facts.music.durationSeconds));
        issue.suggestions = {"place it earlier", "shorten its departure (set leaveSeconds)"};
    }

    // ---- clear air, asked of the world ------------------------------------------------------------------
    const glm::vec2 station = timeline->station;
    if (facts.worldBounds) {
        const auto& [lo, hi] = *facts.worldBounds;
        if (station.x < lo.x || station.y < lo.y || station.x > hi.x || station.y > hi.y) {
            error(IssueCode::SpatialInfeasible, at + "/place",
                  fmt::format("{} is outside the world ({:.0f}..{:.0f}, {:.0f}..{:.0f})", xz(station), lo.x, hi.x, lo.y,
                              hi.y));
        }
    }
    if (!facts.canopyAt) {
        warning(IssueCode::SpatialInfeasible, at + "/place",
                "this scene has no navigation layer, so clear air over the set piece cannot be checked");
    } else if (spec.kind == stage::SetPieceKind::Abduction) {
        const float clearance = slot(spec, "targetClearance");
        const float hover = slot(spec, "hoverHeight");
        const auto clear = [&](glm::vec2 p) {
            const float canopy = facts.canopyAt(p.x, p.y);
            return canopy < hover - kCanopyMargin && (clearance <= 0.0f || canopy <= clearance);
        };
        if (spec.place.kind == stage::SetPiecePlace::Kind::Point) {
            if (!clear(station)) {
                Issue& issue = error(IssueCode::SpatialInfeasible, at + "/place",
                                     fmt::format("{:.1f} m of canopy stands over {}: an abduction needs clear air (at most "
                                                 "{:.1f} m, and under the {:.1f} m hover)",
                                                 facts.canopyAt(station.x, station.y), xz(station), clearance, hover));
                issue.suggestions = {"move it into the open", "raise targetClearance if the canopy is meant to be crossed"};
            }
        } else {
            // A region is searched for a subject at run time, so it is refused only when no part of it
            // could ever offer one: every sample of a 9 x 9 grid across it under canopy.
            int open = 0;
            for (int gz = 0; gz < 9; ++gz) {
                for (int gx = 0; gx < 9; ++gx) {
                    const glm::vec2 p = point + glm::vec2((static_cast<float>(gx) - 4.0f) / 4.0f,
                                                          (static_cast<float>(gz) - 4.0f) / 4.0f) *
                                                    spec.place.radius;
                    if (glm::length(p - point) <= spec.place.radius + 1e-3f && clear(p)) {
                        ++open;
                    }
                }
            }
            if (open == 0) {
                error(IssueCode::SpatialInfeasible, at + "/place",
                      fmt::format("no part of the region ({:.0f} m round {}) has clear air for an abduction",
                                  spec.place.radius, xz(point)));
            }
        }
        for (const std::string& animal : animals) {
            const CharacterMark* mark = facts.character(animal);
            if (mark != nullptr && !clear(glm::vec2(mark->anchor.x, mark->anchor.z))) {
                error(IssueCode::SpatialInfeasible, at + "/animals",
                      fmt::format("{} stands under {:.1f} m of canopy where the scene puts it", animal,
                                  facts.canopyAt(mark->anchor.x, mark->anchor.z)));
            }
        }
    } else {
        // A survey sweeps and a flyby crosses: every point of the line must be above the canopy.
        const float height = spec.kind == stage::SetPieceKind::Survey ? slot(spec, "hoverHeight") : slot(spec, "altitude");
        const glm::vec2 from = spec.kind == stage::SetPieceKind::Survey ? station : timeline->entry;
        const glm::vec2 to = spec.kind == stage::SetPieceKind::Survey
                                 ? station + (2.0f * (point - station))
                                 : timeline->exit;
        for (int k = 0; k <= 16; ++k) {
            const glm::vec2 p = glm::mix(from, to, static_cast<float>(k) / 16.0f);
            const float canopy = facts.canopyAt(p.x, p.y);
            if (canopy >= height - kCanopyMargin) {
                error(IssueCode::SpatialInfeasible, at + "/place",
                      fmt::format("{:.1f} m of canopy at {} reaches the craft's {:.1f} m", canopy, xz(p), height));
                break;
            }
        }
    }

    // ---- will it find its animals? An estimate from where the scene puts them ----------------------------
    if (spec.kind == stage::SetPieceKind::Abduction && animals.empty()) {
        const int wanted = stage::setPieceAnimalCount(spec);
        const float reach = slot(spec, "gatherRadius") +
                            (spec.place.kind == stage::SetPiecePlace::Kind::Region ? spec.place.radius : 0.0f);
        int found = 0;
        for (const CharacterMark& mark : facts.characters) {
            if (std::find(mark.tags.begin(), mark.tags.end(), spec.tag) != mark.tags.end() &&
                glm::length(glm::vec2(mark.anchor.x, mark.anchor.z) - point) <= reach) {
                ++found;
            }
        }
        if (found < wanted) {
            Issue& issue = warning(IssueCode::SpatialInfeasible, at + "/place",
                                   fmt::format("{} animal(s) tagged '{}' stand within {:.0f} m of {} where the scene puts "
                                               "them, and this abduction lifts {}: animals move, but if it finds fewer it "
                                               "leaves without beaming",
                                               found, spec.tag, reach, xz(point), wanted));
            issue.suggestions = {"move it nearer the herd", "raise gatherRadius", "lift fewer animals"};
        }
    }

    // ---- the scenario's name, and the craft's other directors ----------------------------------------------
    const std::string scenario = stage::setPieceScenarioName(sp.key);
    for (const stage::ScenarioDesc& existing : staging.scenarios) {
        if (existing.name == scenario && !producedBefore(plan, facts, scenario)) {
            Issue& issue = error(IssueCode::DuplicateKey, at + "/key",
                                 fmt::format("the scene already has a scenario '{}' that this plan did not make", scenario));
            issue.suggestions = {"give the set piece another key"};
        }
        if (!stage::isSetPieceScenario(existing.name) && existing.actor == craft->name) {
            Issue& issue = add(issues, existing.autoStart ? Severity::Error : Severity::Warning, IssueCode::TimingConflict,
                               sp.key, at + "/craft",
                               existing.autoStart
                                   ? fmt::format("authored scenario '{}' directs '{}' from the start of the film; a set "
                                                 "piece on the same craft would fight it for the body",
                                                 existing.name, craft->name)
                                   : fmt::format("authored scenario '{}' also directs '{}' when it is started; if it runs "
                                                 "during this set piece they will fight for the craft",
                                                 existing.name, craft->name));
            issue.suggestions = {"move that scenario onto set pieces", "use another craft"};
        }
    }

    out.spec = std::move(spec);
    out.timeline = std::move(*timeline);
    return out;
}

std::vector<ResolvedSetPiece> otherPlansSetPieces(const Plan& plan, const SceneFacts& facts) {
    std::vector<ResolvedSetPiece> out;
    for (const Plan& other : facts.plans) {
        if (other.id == plan.id || other.setPieces.empty()) {
            continue;
        }
        Plan copy = other;
        const PlanTimes times = resolvePlanTimes(copy, facts.music);
        std::vector<Issue> ignored;
        for (std::size_t i = 0; i < copy.setPieces.size(); ++i) {
            // Only while its scenario is still in the scene: a set piece somebody deleted by hand has
            // no craft to fight over.
            const std::string scenario = stage::setPieceScenarioName(copy.setPieces[i].key);
            const bool present = std::any_of(facts.staged.staging.scenarios.begin(), facts.staged.staging.scenarios.end(),
                                             [&](const stage::ScenarioDesc& s) { return s.name == scenario; });
            if (!present) {
                continue;
            }
            ResolvedSetPiece r = resolveSetPiece(copy, i, facts, times, ignored);
            if (r.spec && r.timeline) {
                out.push_back(std::move(r));
            }
        }
    }
    return out;
}

void checkSetPiecesTogether(const std::vector<ResolvedSetPiece>& mine, const std::vector<ResolvedSetPiece>& others,
                            const SceneFacts& facts, std::vector<Issue>& issues) {
    (void)facts;
    struct Entry {
        const ResolvedSetPiece* piece;
        bool mine;
    };
    std::vector<Entry> all;
    for (const ResolvedSetPiece& r : mine) {
        if (r.spec && r.timeline) {
            all.push_back({&r, true});
        }
    }
    for (const ResolvedSetPiece& r : others) {
        if (r.spec && r.timeline) {
            all.push_back({&r, false});
        }
    }
    const auto label = [](const ResolvedSetPiece& r) {
        return r.plan.empty() ? fmt::format("'{}'", r.key) : fmt::format("'{}' (plan {})", r.key, r.plan);
    };

    // ---- one craft, in time order ---------------------------------------------------------------------
    std::vector<Entry> byTime = all;
    std::stable_sort(byTime.begin(), byTime.end(), [](const Entry& a, const Entry& b) {
        return a.piece->spec->actor != b.piece->spec->actor ? a.piece->spec->actor < b.piece->spec->actor
                                                            : a.piece->timeline->start < b.piece->timeline->start;
    });
    for (std::size_t i = 1; i < byTime.size(); ++i) {
        const Entry& a = byTime[i - 1];
        const Entry& b = byTime[i];
        if (a.piece->spec->actor != b.piece->spec->actor || (!a.mine && !b.mine)) {
            continue;
        }
        const Entry& blamed = b.mine ? b : a;
        const std::string craft = b.piece->spec->actor;
        const double free = a.piece->timeline->end;
        const double needed = b.piece->timeline->start;
        if (needed < free + stage::kCraftHandoverSeconds) {
            Issue& issue = add(issues, Severity::Error, IssueCode::TimingConflict, blamed.piece->key, "",
                               fmt::format("one craft in two places: {} needs '{}' from {:.3f}s, and {} has it until "
                                           "{:.3f}s (with {:.1f}s to hand over)",
                                           label(*b.piece), craft, needed, label(*a.piece), free,
                                           stage::kCraftHandoverSeconds));
            issue.details = {{"craft", craft}, {"free", free}, {"needed", needed}, {"other", (b.mine ? a : b).piece->key}};
            issue.suggestions = {fmt::format("start it after {:.3f}s", free + stage::kCraftHandoverSeconds),
                                 "use another craft", "shorten the earlier one's departure (set leaveSeconds)"};
            continue;
        }
        // Travel the craft could not make: from where the earlier one leaves it to where the later one
        // needs it to appear, in the time between, at no more than the later one's cruise speed.
        const float distance = glm::length(b.piece->timeline->entry - a.piece->timeline->exit);
        const double arrive = needed + static_cast<double>(stage::setPieceValue(*b.piece->spec, "transitSeconds"));
        const double window = arrive - free;
        const float cruise = stage::setPieceValue(*b.piece->spec, "cruiseSpeed");
        const double speed = static_cast<double>(distance) / std::max(window, 1e-3);
        if (speed > static_cast<double>(cruise)) {
            Issue& issue = add(issues, Severity::Error, IssueCode::TimingConflict, blamed.piece->key, "",
                               fmt::format("travel '{}' cannot make: it leaves {} at {} at {:.3f}s and must appear for {} "
                                           "at {} by {:.3f}s -- {:.0f} m in {:.2f}s is {:.0f} m/s, and it cruises at {:.0f} m/s",
                                           craft, label(*a.piece), xz(a.piece->timeline->exit), free, label(*b.piece),
                                           xz(b.piece->timeline->entry), arrive, distance, window, speed, cruise));
            issue.details = {{"craft", craft}, {"metres", distance}, {"seconds", window}, {"needs", speed}, {"cruise", cruise}};
            issue.suggestions = {"leave more time between them", "bring the later one in from nearer the earlier one's exit",
                                 "raise cruiseSpeed if the craft may fly that fast"};
        }
    }

    // ---- the same shot twice ----------------------------------------------------------------------------
    for (std::size_t i = 0; i < all.size(); ++i) {
        for (std::size_t j = i + 1; j < all.size(); ++j) {
            const Entry& x = all[i];
            const Entry& y = all[j];
            if (!x.mine && !y.mine) {
                continue;
            }
            // Blame the later one of this plan's: it is the one that repeats.
            const Entry& blamed = !y.mine ? x : (!x.mine ? y : (y.piece->timeline->start >= x.piece->timeline->start ? y : x));
            const Entry& other = &blamed == &x ? y : x;
            // A flyby happens at no place -- its "station" is the middle of a crossing -- so it repeats
            // only another flyby's line; a crossing over where an abduction later happens is a rhyme,
            // not the same shot (GV3's flyby passes over the elder its centrepiece lifts beside).
            const bool flybyX = x.piece->spec->kind == stage::SetPieceKind::Flyby;
            const bool flybyY = y.piece->spec->kind == stage::SetPieceKind::Flyby;
            const float apart = glm::length(x.piece->timeline->station - y.piece->timeline->station);
            if (flybyX == flybyY && apart < kSamePlaceMetres) {
                Issue& issue = add(issues, Severity::Warning, IssueCode::Repetition, blamed.piece->key, "",
                                   fmt::format("{} and {} happen {:.0f} m apart, at {} and {}: do not duplicate the same "
                                               "abduction shot -- vary the place",
                                               label(*blamed.piece), label(*other.piece), apart,
                                               xz(blamed.piece->timeline->station), xz(other.piece->timeline->station)));
                issue.details = {{"other", other.piece->key}, {"metres", apart}};
                issue.suggestions = {fmt::format("move it at least {:.0f} m from {}", kSamePlaceMetres, other.piece->key)};
            }
            if (x.piece->spec->kind == y.piece->spec->kind && x.piece->framingMetres && y.piece->framingMetres) {
                const float fx = *x.piece->framingMetres;
                const float fy = *y.piece->framingMetres;
                if (std::abs(fx - fy) <= kSameFramingFraction * std::max(fx, fy)) {
                    Issue& issue = add(issues, Severity::Warning, IssueCode::Repetition, blamed.piece->key, "",
                                       fmt::format("{} and {} are both {}s framed from about the same distance ({:.0f} m "
                                                   "and {:.0f} m): vary the framing -- one close, one far",
                                                   label(*blamed.piece), label(*other.piece),
                                                   stage::setPieceKindName(x.piece->spec->kind), fx, fy));
                    issue.details = {{"other", other.piece->key}, {"framing", {fx, fy}}};
                }
            }
        }
    }
}

nlohmann::json setPieceCatalog(const SceneFacts& facts) {
    nlohmann::json templates = nlohmann::json::array();
    for (const std::string& name : stage::setPieceKindNames()) {
        const auto kind = stage::setPieceKindFromName(name);
        nlohmann::json slots = nlohmann::json::array();
        for (const stage::SetPieceSlot& slot : stage::setPieceSlots(*kind)) {
            slots.push_back({{"name", slot.name},
                             {"default", slot.value},
                             {"min", slot.min},
                             {"max", slot.max},
                             {"unit", slot.unit},
                             {"label", slot.label},
                             {"kind", slot.use == stage::SlotUse::Parameter ? "knob" : "structure"}});
        }
        templates.push_back({{"name", name},
                             {"moments", stage::setPieceMoments(*kind)},
                             {"defaultMoment", stage::defaultSetPieceMoment(*kind)},
                             {"regionPlace", *kind == stage::SetPieceKind::Abduction},
                             {"slots", std::move(slots)}});
    }
    nlohmann::json crafts = nlohmann::json::array();
    for (const stage::ActorDesc& actor : facts.staged.staging.actors) {
        const bool beam = std::any_of(actor.parts.begin(), actor.parts.end(),
                                      [](const stage::ActorPart& p) { return p.name == "beam"; });
        std::vector<std::string> busy;
        for (const stage::ScenarioDesc& s : facts.staged.staging.scenarios) {
            if (s.actor == actor.name && !stage::isSetPieceScenario(s.name)) {
                busy.push_back(s.name + (s.autoStart ? " (from the start: set pieces on it are refused)" : ""));
            }
        }
        nlohmann::json craft{{"name", actor.name}, {"entity", actor.driven()}, {"beam", beam},
                             {"templates", beam ? stage::setPieceKindNames() : std::vector<std::string>{"flyby"}}};
        if (!busy.empty()) {
            craft["authoredScenarios"] = busy;
        }
        crafts.push_back(std::move(craft));
    }
    return {{"templates", std::move(templates)},
            {"crafts", std::move(crafts)},
            {"rules",
             {fmt::format("one craft plays its set pieces one after another: {:.1f} s at least between one letting it "
                          "go and the next taking it, and no faster than the later one's cruiseSpeed in between",
                          stage::kCraftHandoverSeconds),
              "the time places one moment (\"moment\", default the beam, or a flyby's crossing); the others follow "
              "at the template's durations and are reported by avgen --plan-report",
              fmt::format("vary them: two within {:.0f} m of each other, or two of one template framed within {:.0f}% "
                          "of the same distance, are flagged REPETITION",
                          kSamePlaceMetres, kSameFramingFraction * 100.0f),
              "every moment is a world event and a bus event, setpiece/<key>/<moment>: a cue can start on it "
              "(\"on\") and a route can key on it"}}};
}

std::optional<std::size_t> setPieceOfEvent(const Plan& plan, std::string_view event) {
    if (!event.starts_with("setpiece/")) {
        return std::nullopt;
    }
    const std::string_view rest = event.substr(9);
    const std::size_t slash = rest.rfind('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view key = rest.substr(0, slash);
    const std::string_view moment = rest.substr(slash + 1);
    for (std::size_t i = 0; i < plan.setPieces.size(); ++i) {
        if (plan.setPieces[i].key != key) {
            continue;
        }
        const auto kind = stage::setPieceKindFromName(plan.setPieces[i].templateName);
        if (!kind) {
            return std::nullopt;
        }
        const std::vector<std::string> moments = stage::setPieceMoments(*kind);
        if (std::find(moments.begin(), moments.end(), moment) == moments.end()) {
            return std::nullopt;
        }
        return i;
    }
    return std::nullopt;
}

} // namespace avgen::directing
