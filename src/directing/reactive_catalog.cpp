#include "directing/reactive_catalog.hpp"

#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/light_rig.hpp"
#include "scene/material_program.hpp"
#include "stage/staging.hpp"
#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_kind.hpp"
#include "world/hero.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <tuple>

namespace avgen::directing {
namespace {

using json = nlohmann::json;
using params::ModOp;

constexpr std::array<std::pair<ReactiveKind, const char*>, 4> kKinds{{
    {ReactiveKind::Luminance, "luminance"},
    {ReactiveKind::Hue, "hue"},
    {ReactiveKind::Motion, "motion"},
    {ReactiveKind::Density, "density"},
}};

constexpr std::array<std::pair<ReactiveGroup, const char*>, 15> kGroups{{
    {ReactiveGroup::HeroEmission, "hero-emission"},
    {ReactiveGroup::NodeEmission, "node-emission"},
    {ReactiveGroup::MaterialEmission, "material-emission"},
    {ReactiveGroup::ScatterGlow, "scatter-glow"},
    {ReactiveGroup::ScatterHue, "scatter-hue"},
    {ReactiveGroup::ScatterWave, "scatter-wave"},
    {ReactiveGroup::NodeWave, "node-wave"},
    {ReactiveGroup::Particles, "particles"},
    {ReactiveGroup::Effect, "effect"},
    {ReactiveGroup::Light, "light"},
    {ReactiveGroup::EcologyLight, "ecology-light"},
    {ReactiveGroup::Atmosphere, "atmosphere"},
    {ReactiveGroup::Wind, "wind"},
    {ReactiveGroup::Water, "water"},
    {ReactiveGroup::WaterTears, "water-tears"},
}};
static_assert(kGroups.back().first == ReactiveGroup::WaterTears);

constexpr std::array<ReactiveGroup, 15> kAllGroups = [] {
    std::array<ReactiveGroup, 15> out{};
    for (std::size_t i = 0; i < kGroups.size(); ++i) {
        out[i] = kGroups[i].first;
    }
    return out;
}();

std::vector<std::string_view> split(std::string_view path) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? path.size() : slash;
        out.push_back(path.substr(start, end - start));
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return out;
}

bool isBlack(const glm::vec3& c) {
    return c.x <= 0.0f && c.y <= 0.0f && c.z <= 0.0f;
}

const scene::MaterialProgram* programNamed(const scene::Composition& comp, std::string_view name) {
    for (const scene::MaterialProgram& p : comp.scene().materialPrograms) {
        if (p.name == name) {
            return &p;
        }
    }
    return nullptr;
}

// Whether anything a scatter layer draws emits: its own emissive colour and intensity, or a program
// that writes emission (the program then owns the colour, ADR-179, and the layer's lane multiplies
// it, ADR-905).
bool layerEmits(const scene::Composition& comp, const world::ScatterLayer& layer) {
    if (layer.emissiveIntensity > 0.0f && !isBlack(layer.emissiveColor)) {
        return true;
    }
    if (!layer.materialProgram.empty()) {
        if (const scene::MaterialProgram* p = programNamed(comp, layer.materialProgram)) {
            return p->writesEmission();
        }
    }
    return false;
}

// How brightly a scatter layer glows as authored: its intensity times its colour's brightest channel,
// or its program's intensity when it authors none (the program then owns the colour, ADR-179).
float layerEmission(const scene::Composition& comp, const world::ScatterLayer& layer) {
    if (layer.emissiveIntensity > 0.0f && !isBlack(layer.emissiveColor)) {
        return layer.emissiveIntensity * std::max({layer.emissiveColor.x, layer.emissiveColor.y, layer.emissiveColor.z});
    }
    if (const scene::MaterialProgram* p = programNamed(comp, layer.materialProgram); p != nullptr && p->writesEmission()) {
        return p->emissionIntensity;
    }
    return 0.0f;
}

// The brightest surface a node draws itself, and whether a program lights it.
std::pair<float, bool> nodeEmission(const scene::Composition& comp, std::string_view node) {
    float best = 0.0f;
    bool programLit = false;
    const scene::Scene& s = comp.scene();
    const auto surface = [&](const scene::Material& m) {
        if (!m.program.empty()) {
            if (const scene::MaterialProgram* p = programNamed(comp, m.program); p != nullptr && p->writesEmission()) {
                best = std::max(best, p->emissionIntensity);
                programLit = true;
                return;
            }
        }
        if (m.emissiveIntensity > 0.0f) {
            best = std::max(best, m.emissiveIntensity * std::max({m.emissiveColor.x, m.emissiveColor.y, m.emissiveColor.z}));
        }
    };
    for (std::size_t i = 0; i < s.procedurals.size(); ++i) {
        if (const scene::CompositionNode* n = comp.nodeForProcedural(i); n != nullptr && n->name == node) {
            surface(s.procedurals[i].material);
        }
    }
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        if (const scene::CompositionNode* n = comp.nodeForEntity(i); n != nullptr && n->name == node) {
            surface(s.entities[i].material);
        }
    }
    return {best, programLit};
}

// The family table: what a registered parameter path is, as a reactive target. The only vocabulary
// in the catalogue that is not read from the scene -- that `emissionGain` is a glow and `windSpeed`
// is motion -- and every family here has a case in test_reactive_catalog.cpp.
struct Candidate {
    ReactiveGroup group = ReactiveGroup::NodeEmission;
    ReactiveKind kind = ReactiveKind::Luminance;
    ReactiveLevel level = ReactiveLevel::Meso;
    std::string owner;
    std::string label;
    ModOp op = ModOp::Multiply;
    // The safe range: relative to the base for a multiply (lo, hi are factors), an offset around the
    // base for an add (lo, hi are added), or absolute when `absolute`.
    float lo = 0.8f;
    float hi = 1.2f;
    bool absolute = false;
    bool global = false;
    std::optional<glm::vec3> position;
    float size = 0.0f;
    std::string field;
    bool fieldTriggered = false;
    std::vector<std::string> sharedBy;
    std::string heroNode; // the node whose hero the target belongs to ("" = look it up by owner)
    float emission = 0.0f;
    bool programLit = false;
    std::string excluded; // why this family member is not offered, when it is not
};

struct SceneIndex {
    const scene::Composition* comp = nullptr;
    std::map<std::string, const scene::CompositionNode*, std::less<>> nodes;
    std::map<std::string, std::string, std::less<>> heroOf; // node -> hero
    std::vector<ReactiveHero> heroes;
    std::set<std::string, std::less<>> scripted; // characters, what staging moves, and what is under them

    [[nodiscard]] const scene::CompositionNode* node(std::string_view name) const {
        const auto it = nodes.find(name);
        return it == nodes.end() ? nullptr : it->second;
    }
    [[nodiscard]] std::string hero(std::string_view node) const {
        const auto it = heroOf.find(node);
        return it == heroOf.end() ? std::string() : it->second;
    }
};

// Which hero each node answers for: the node the hero point names (ADR-107: a hero is one object),
// every node standing within the hero's reach, and every descendant of either. Characters -- nodes
// an entity drives -- are no hero's part: where they stand is their simulation's, not the scene's.
SceneIndex indexScene(const scene::Composition* comp) {
    SceneIndex index;
    index.comp = comp;
    if (comp == nullptr) {
        return index;
    }
    std::set<std::string, std::less<>> driven;
    std::vector<std::string> every;
    for (const entity::EntityDesc& e : comp->entities()) {
        driven.insert(e.node.empty() ? e.name : e.node);
        every.push_back(e.name);
    }
    // Whatever a staging scenario can move -- its actors, their parts, whatever its queries bind.
    const auto nodeOf = [comp](const std::string& entity) {
        for (const entity::EntityDesc& e : comp->entities()) {
            if (e.name == entity) {
                return e.node.empty() ? e.name : e.node;
            }
        }
        return entity;
    };
    const auto tagsOf = [comp](const std::string& entity) {
        for (const entity::EntityDesc& e : comp->entities()) {
            if (e.name == entity) {
                return e.tags;
            }
        }
        return std::vector<std::string>{};
    };
    for (const std::string& n : stage::scenarioOwnedNodes(comp->staging(), nodeOf, tagsOf, every)) {
        driven.insert(n);
    }
    for (const auto& node : comp->nodes()) {
        if (node != nullptr) {
            index.nodes.emplace(node->name, node.get());
        }
    }
    // ...and everything under them: a character's lights and a craft's beam are theirs.
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [name, node] : index.nodes) {
            if (!driven.contains(name) && !node->parent.empty() && driven.contains(node->parent)) {
                driven.insert(name);
                grew = true;
            }
        }
    }
    for (const world::HeroPoint& h : comp->heroes()) {
        ReactiveHero hero;
        hero.name = h.name;
        hero.position = h.position;
        hero.radius = std::max(h.radius, 0.1f);
        hero.importance = h.importance;
        index.heroes.push_back(std::move(hero));
    }
    // Direct membership: the named node, or the nearest hero whose reach holds the node.
    for (const auto& [name, node] : index.nodes) {
        if (node->kind == scene::NodeKind::Terrain || node->kind == scene::NodeKind::Field ||
            node->kind == scene::NodeKind::Spline) {
            continue;
        }
        if (driven.contains(name)) {
            continue;
        }
        std::string best;
        for (const ReactiveHero& h : index.heroes) {
            if (h.name == name) {
                best = h.name;
            }
        }
        if (best.empty() && node->parent.empty()) {
            const glm::vec3 p = comp->nodeWorldTransform(*node).position;
            float bestDistance = 0.0f;
            for (const ReactiveHero& h : index.heroes) {
                if (driven.contains(h.name)) {
                    continue; // a character's hero point moves with the character
                }
                const float d = glm::length(glm::vec2(p.x - h.position.x, p.z - h.position.z));
                const float reach = std::max(h.radius * 2.0f, h.radius + 3.0f);
                if (d <= reach && (best.empty() || d < bestDistance)) {
                    best = h.name;
                    bestDistance = d;
                }
            }
        }
        if (!best.empty()) {
            index.heroOf[name] = best;
        }
    }
    // Descendants inherit: a hero's spores are parented to its cap.
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [name, node] : index.nodes) {
            if (index.heroOf.contains(name) || node->parent.empty() || driven.contains(name)) {
                continue;
            }
            if (const auto parent = index.heroOf.find(node->parent); parent != index.heroOf.end()) {
                index.heroOf[name] = parent->second;
                grew = true;
            }
        }
    }
    for (ReactiveHero& h : index.heroes) {
        for (const auto& [node, hero] : index.heroOf) {
            if (hero == h.name) {
                h.members.push_back(node);
            }
        }
    }
    // A hero point with nothing of the scene's own standing for it (a character's) answers nothing.
    std::erase_if(index.heroes, [](const ReactiveHero& h) { return h.members.empty(); });
    index.scripted.insert(driven.begin(), driven.end());
    return index;
}

std::optional<glm::vec3> nodePosition(const SceneIndex& index, std::string_view name) {
    if (index.comp == nullptr) {
        return std::nullopt;
    }
    if (const scene::CompositionNode* n = index.node(name)) {
        return index.comp->nodeWorldTransform(*n).position;
    }
    return std::nullopt;
}

float heroRadius(const SceneIndex& index, std::string_view hero) {
    for (const ReactiveHero& h : index.heroes) {
        if (h.name == hero) {
            return h.radius;
        }
    }
    return 0.0f;
}

// Every node and scatter layer drawn with a material program.
std::vector<std::string> surfacesOnProgram(const scene::Composition& comp, std::string_view program) {
    std::set<std::string> out;
    const scene::Scene& s = comp.scene();
    for (std::size_t i = 0; i < s.procedurals.size(); ++i) {
        if (s.procedurals[i].material.program == program) {
            if (const scene::CompositionNode* n = comp.nodeForProcedural(i)) {
                if (n->kind != scene::NodeKind::Terrain) {
                    out.insert(n->name);
                }
            }
        }
    }
    for (std::size_t i = 0; i < s.entities.size(); ++i) {
        if (s.entities[i].material.program == program) {
            if (const scene::CompositionNode* n = comp.nodeForEntity(i)) {
                out.insert(n->name);
            }
        }
    }
    for (const auto& node : comp.nodes()) {
        if (node != nullptr && node->kind == scene::NodeKind::Terrain) {
            for (const world::ScatterLayer& layer : node->ecology.layers) {
                if (layer.materialProgram == program) {
                    out.insert(node->name + "/" + layer.name);
                }
            }
        }
    }
    return {out.begin(), out.end()};
}

std::optional<Candidate> classify(std::string_view path, const params::IParameter& p, const SceneIndex& index,
                                  bool anyLayerEmits) {
    const std::vector<std::string_view> seg = split(path);
    const float base = p.baseComponent(0);
    const scene::Composition* comp = index.comp;
    Candidate c;

    // ---- nodes/<n>/emissiveBoost: a node's own glow, after its program (ADR-903) --------------------
    if (seg.size() == 3 && seg[0] == "nodes" && seg[2] == "emissiveBoost") {
        const scene::CompositionNode* node = index.node(seg[1]);
        if (node == nullptr) {
            return std::nullopt;
        }
        if (node->kind == scene::NodeKind::Terrain) {
            c.excluded = "a terrain's boost moves every scatter layer at once; its layers' own lanes are the handle";
            return c;
        }
        if (node->kind == scene::NodeKind::Particles) {
            c.excluded = "a particle node's glow is its particles/<n>/emissive";
            return c;
        }
        c.owner = std::string(seg[1]);
        c.heroNode = c.owner;
        c.group = index.hero(c.owner).empty() ? ReactiveGroup::NodeEmission : ReactiveGroup::HeroEmission;
        c.kind = ReactiveKind::Luminance;
        c.level = ReactiveLevel::Meso;
        c.label = c.owner + " glow";
        c.op = ModOp::Add;
        c.lo = -0.4f;
        c.hi = 1.0f; // a hero's pulse: +60%, up to +100% in the drop (the GV3 revision plan section 5)
        c.position = nodePosition(index, c.owner);
        if (comp != nullptr) {
            std::tie(c.emission, c.programLit) = nodeEmission(*comp, c.owner);
        }
        return c;
    }

    // ---- nodes/<terrain>/scatter/<layer>/<lane> (ADR-905) -------------------------------------------
    if (seg.size() == 5 && seg[0] == "nodes" && seg[2] == "scatter" && comp != nullptr) {
        const scene::CompositionNode* terrain = index.node(seg[1]);
        if (terrain == nullptr) {
            return std::nullopt;
        }
        const world::ScatterLayer* layer = nullptr;
        for (const world::ScatterLayer& l : terrain->ecology.layers) {
            if (l.name == seg[3]) {
                layer = &l;
            }
        }
        if (layer == nullptr) {
            return std::nullopt;
        }
        c.owner = fmt::format("{}/{}", seg[1], seg[3]);
        c.size = layer->height;
        c.emission = layerEmission(*comp, *layer);
        if (const scene::MaterialProgram* program = programNamed(*comp, layer->materialProgram)) {
            c.programLit = program->writesEmission();
        }
        const bool emits = layerEmits(*comp, *layer);
        if (seg[4] == "emissionGain" || seg[4] == "hueOffset") {
            if (!emits) {
                c.excluded = fmt::format("layer '{}' emits nothing: no emissive colour and no program that writes "
                                         "emission, so its glow and hue lanes multiply zero",
                                         seg[3]);
                return c;
            }
            if (seg[4] == "emissionGain") {
                c.group = ReactiveGroup::ScatterGlow;
                c.kind = ReactiveKind::Luminance;
                // Small things answer fast layers; a layer of lamps stands with the heroes.
                c.level = layer->height < 1.0f ? ReactiveLevel::Micro : ReactiveLevel::Meso;
                c.label = std::string(seg[3]) + " glow";
                c.op = ModOp::Add;
                c.lo = -0.3f; // +-30% (the emission stream's measurement on GV3's valley)
                c.hi = 0.4f;
            } else {
                c.group = ReactiveGroup::ScatterHue;
                c.kind = ReactiveKind::Hue;
                c.level = ReactiveLevel::Macro;
                c.label = std::string(seg[3]) + " hue shift";
                c.op = ModOp::Add;
                c.lo = -0.08f; // turns: past +0.08 the valley's teal clips (the emission stream)
                c.hi = 0.08f;
            }
            return c;
        }
        if (seg[4] == "emissiveFieldAmount") {
            if (!emits) {
                c.excluded = fmt::format("layer '{}' emits nothing, so a light wave through it lights nothing", seg[3]);
                return c;
            }
            if (layer->emissiveField.empty()) {
                c.excluded = fmt::format("layer '{}' names no emissiveField, so its light-wave depth multiplies "
                                         "nothing",
                                         seg[3]);
                return c;
            }
            const spatial::FieldSpec* field = comp->scene().fields.find(layer->emissiveField);
            if (field == nullptr) {
                c.excluded = fmt::format("layer '{}' names field '{}', which the scene does not have", seg[3],
                                         layer->emissiveField);
                return c;
            }
            c.group = ReactiveGroup::ScatterWave;
            c.kind = ReactiveKind::Luminance;
            c.level = ReactiveLevel::Meso;
            c.label = std::string(seg[3]) + " light wave";
            c.field = layer->emissiveField;
            c.fieldTriggered = field->trigger.has_value();
            c.op = base > 0.0f ? ModOp::Multiply : ModOp::Add;
            c.lo = 0.0f;
            c.hi = std::max(10.0f, base * 1.25f); // 3 is subtle, 8 clear (the emission stream)
            c.absolute = true;
            return c;
        }
        return std::nullopt;
    }

    // ---- procedural/<n>/emissiveFieldAmount: a node that names a field ------------------------------
    if (seg.size() == 3 && seg[0] == "procedural" && seg[2] == "emissiveFieldAmount" && comp != nullptr) {
        const scene::CompositionNode* node = index.node(seg[1]);
        if (node == nullptr) {
            return std::nullopt;
        }
        if (node->procedural.emissiveField.empty()) {
            c.excluded = fmt::format("node '{}' names no emissiveField", seg[1]);
            return c;
        }
        const spatial::FieldSpec* field = comp->scene().fields.find(node->procedural.emissiveField);
        if (field == nullptr) {
            c.excluded = fmt::format("node '{}' names field '{}', which the scene does not have", seg[1],
                                     node->procedural.emissiveField);
            return c;
        }
        c.group = ReactiveGroup::NodeWave;
        c.kind = ReactiveKind::Luminance;
        c.level = ReactiveLevel::Meso;
        c.owner = std::string(seg[1]);
        c.heroNode = c.owner;
        c.label = c.owner + " light wave";
        c.field = node->procedural.emissiveField;
        c.fieldTriggered = field->trigger.has_value();
        c.op = base > 0.0f ? ModOp::Multiply : ModOp::Add;
        c.lo = 0.0f;
        c.hi = std::max(10.0f, base * 1.25f);
        c.absolute = true;
        c.position = nodePosition(index, c.owner);
        return c;
    }

    // ---- material/<p>/emissionIntensity and material/<p>/layer/<i>/<name>/emissionIntensity ---------
    if (seg[0] == "material" && seg.back() == "emissionIntensity" && (seg.size() == 3 || (seg.size() == 6 && seg[2] == "layer")) &&
        comp != nullptr) {
        c.group = ReactiveGroup::MaterialEmission;
        c.kind = ReactiveKind::Luminance;
        c.level = ReactiveLevel::Meso;
        c.owner = fmt::format("material/{}", seg[1]);
        c.label = seg.size() == 3 ? fmt::format("{} glow (every surface drawn with it)", seg[1])
                                  : fmt::format("{} {} glow (every surface drawn with it)", seg[1], seg[4]);
        c.sharedBy = surfacesOnProgram(*comp, seg[1]);
        c.op = ModOp::Multiply;
        c.lo = 0.7f;
        c.hi = 1.6f;
        return c;
    }

    // ---- particles/<n>/emissive | spawnRate --------------------------------------------------------------
    if (seg.size() == 3 && seg[0] == "particles" && (seg[2] == "emissive" || seg[2] == "spawnRate")) {
        c.group = ReactiveGroup::Particles;
        c.owner = std::string(seg[1]);
        c.heroNode = c.owner;
        c.level = ReactiveLevel::Micro;
        c.op = ModOp::Multiply;
        c.position = nodePosition(index, c.owner);
        if (base <= 0.0f) {
            c.excluded = fmt::format("'{}' is 0, and a multiply of 0 is 0", path);
            return c;
        }
        if (seg[2] == "emissive") {
            c.kind = ReactiveKind::Luminance;
            c.label = c.owner + " glow";
            c.lo = 0.7f;
            c.hi = 1.35f; // GV3's own treble routes: x0.7-1.35
        } else {
            c.kind = ReactiveKind::Density;
            c.label = c.owner + " density";
            c.lo = 0.5f;
            c.hi = 1.8f;
        }
        return c;
    }

    // ---- fx/<id>/intensity | glow | gain ---------------------------------------------------------------
    if (seg.size() == 3 && seg[0] == "fx" && (seg[2] == "intensity" || seg[2] == "glow" || seg[2] == "gain")) {
        c.group = ReactiveGroup::Effect;
        c.kind = ReactiveKind::Luminance;
        c.owner = std::string(seg[1]);
        c.op = ModOp::Multiply;
        c.label = fmt::format("{} {}", seg[1], seg[2]);
        c.level = ReactiveLevel::Meso;
        c.lo = 0.7f;
        c.hi = 1.6f;
        if (base <= 0.0f) {
            c.excluded = fmt::format("'{}' is 0, and a multiply of 0 is 0", path);
        }
        return c; // the owner kind and hero are filled from the effect list by the caller
    }

    // ---- lightrig/<rig>/<light>/intensity, lightrig/<rig>/keyIntensity|ambientIntensity -------------
    if (seg[0] == "lightrig" && ((seg.size() == 4 && seg[3] == "intensity") ||
                                 (seg.size() == 3 && (seg[2] == "keyIntensity" || seg[2] == "ambientIntensity")))) {
        c.group = ReactiveGroup::Light;
        c.kind = ReactiveKind::Luminance;
        c.op = ModOp::Multiply;
        if (seg.size() == 3) {
            c.owner = "world";
            c.global = true;
            c.level = ReactiveLevel::Macro;
            c.label = seg[2] == "keyIntensity" ? "key light" : "ambient light";
            c.lo = 0.9f;
            c.hi = 1.1f;
            return c;
        }
        c.owner = std::string(seg[2]);
        c.label = c.owner + " light";
        const scene::LightRig* rig = comp != nullptr ? comp->lightRig() : nullptr;
        bool practical = false;
        if (rig != nullptr) {
            for (const scene::RigLight& l : rig->lights) {
                if (l.name == seg[2]) {
                    practical = l.role == scene::PunctualLight::Role::Practical;
                }
            }
        }
        if (comp != nullptr) {
            for (const scene::PunctualLight& l : comp->scene().lights) {
                if (l.name == seg[2]) {
                    c.position = l.position;
                }
            }
            // Before the composition's first frame its rig is not expanded into the scene yet: expand it
            // here around the subject the composition sizes its rig to (its framed focal point, else the
            // whole scene), as `Composition::update` does.
            if (!c.position && rig != nullptr) {
                glm::vec3 centre = comp->boundsCenter();
                float radius = comp->boundsRadius();
                const scene::CompositionData& data = comp->composition();
                const scene::FocalPoint* focus = data.cameraTarget.empty()
                                                     ? (data.focalPoints.empty() ? nullptr : &data.focalPoints.front())
                                                     : data.find(data.cameraTarget);
                if (focus != nullptr) {
                    centre = focus->position;
                    radius = std::max(focus->radius, 1e-3f);
                }
                for (const scene::PunctualLight& l :
                     rig->expand(centre, radius, comp->scene().camera.position, glm::vec3(0.0f, 1.0f, 0.0f))) {
                    if (l.name == seg[2]) {
                        c.position = l.position;
                    }
                }
            }
        }
        c.global = !practical;
        c.level = practical ? ReactiveLevel::Meso : ReactiveLevel::Macro;
        c.lo = practical ? 0.7f : 0.9f;
        c.hi = practical ? 1.6f : 1.1f;
        return c;
    }

    // ---- the world: the ecology light, the air, the wind -------------------------------------------------
    if (path == "scene/ecologyLight") {
        if (!anyLayerEmits || base <= 0.0f) {
            c.excluded = base <= 0.0f ? "the ecology light is 0" : "no scatter layer glows, so there is no ecology light";
            return c;
        }
        c.group = ReactiveGroup::EcologyLight;
        c.kind = ReactiveKind::Luminance;
        c.level = ReactiveLevel::Macro;
        c.owner = "world";
        c.label = "light cast by glowing plants and fungi";
        c.lo = 0.55f; // 0.8 in a break to about 2.0 at a drop, on GV3's 1.4 (the emission stream)
        c.hi = 1.45f;
        return c;
    }
    if (path == "scene/volumeDensity" || path == "scene/volumeScattering") {
        c.group = ReactiveGroup::Atmosphere;
        c.kind = path == "scene/volumeDensity" ? ReactiveKind::Density : ReactiveKind::Luminance;
        c.level = ReactiveLevel::Macro;
        c.owner = "world";
        c.label = path == "scene/volumeDensity" ? "fog density" : "fog glow (how much light the air scatters)";
        c.lo = 0.8f;
        c.hi = 1.3f;
        if (base <= 0.0f) {
            c.excluded = fmt::format("'{}' is 0: the scene has no fog to thicken", path);
        }
        return c;
    }
    if (path == "scene/windSpeed" || path == "scene/wind/gustAmount" || path == "scene/wind/turbulence") {
        c.group = ReactiveGroup::Wind;
        c.kind = ReactiveKind::Motion;
        c.level = ReactiveLevel::Macro;
        c.owner = "world";
        c.label = path == "scene/windSpeed" ? "wind strength" : (path == "scene/wind/gustAmount" ? "wind gusts" : "wind turbulence");
        c.lo = path == "scene/windSpeed" ? 0.6f : 0.7f;
        c.hi = path == "scene/windSpeed" ? 1.4f : (path == "scene/wind/gustAmount" ? 1.8f : 2.0f);
        if (base <= 0.0f) {
            c.excluded = fmt::format("'{}' is 0, and a multiply of 0 is 0", path);
        }
        return c;
    }

    // ---- nodes/<terrain>/water/<leaf> ------------------------------------------------------------------
    if (seg.size() == 4 && seg[0] == "nodes" && seg[2] == "water") {
        const scene::CompositionNode* terrain = index.node(seg[1]);
        if (terrain == nullptr || terrain->kind != scene::NodeKind::Terrain) {
            return std::nullopt;
        }
        static constexpr std::array<std::pair<std::string_view, ReactiveKind>, 8> kLeaves{{
            {"glow", ReactiveKind::Luminance}, {"sparkle", ReactiveKind::Luminance}, {"ripple", ReactiveKind::Motion},
            {"swell", ReactiveKind::Motion}, {"foam", ReactiveKind::Luminance}, {"tears", ReactiveKind::Motion},
            {"tearShear", ReactiveKind::Motion}, {"tearCoverage", ReactiveKind::Motion},
        }};
        const auto leaf = std::find_if(kLeaves.begin(), kLeaves.end(), [&](const auto& l) { return l.first == seg[3]; });
        if (leaf == kLeaves.end()) {
            return std::nullopt;
        }
        const bool tears = seg[3].substr(0, 4) == "tear";
        c.group = tears ? ReactiveGroup::WaterTears : ReactiveGroup::Water;
        c.kind = leaf->second;
        c.level = ReactiveLevel::Meso;
        c.owner = fmt::format("{}/water", seg[1]);
        c.label = tears ? (seg[3] == "tears" ? "water tears" : (seg[3] == "tearShear" ? "tear shear" : "tear coverage"))
                        : fmt::format("water {}", seg[3]);
        const bool holdsWater = std::any_of(terrain->worldMap.features.begin(), terrain->worldMap.features.end(),
                                            [](const world::Feature& f) { return f.water; });
        if (!holdsWater) {
            c.excluded = fmt::format("terrain '{}' holds no water", seg[1]);
            return c;
        }
        if (tears && base <= 0.0f) {
            c.excluded = "the water's tears are off (at 0 the tear code is compiled out), so no route can open them";
            return c;
        }
        if (base > 0.0f) {
            c.op = ModOp::Multiply;
            c.lo = 0.8f;
            c.hi = seg[3] == "sparkle" ? 1.5f : 1.4f;
        } else {
            c.op = ModOp::Add;
            c.lo = 0.0f;
            c.hi = std::max(p.softMax(0) * 0.1f, 0.01f);
        }
        return c;
    }
    return std::nullopt;
}

void addTo(std::vector<std::string>& list, const std::string& value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) {
        list.push_back(value);
    }
}

} // namespace

const char* reactiveKindName(ReactiveKind kind) {
    for (const auto& [k, n] : kKinds) {
        if (k == kind) {
            return n;
        }
    }
    return "luminance";
}

const char* reactiveGroupName(ReactiveGroup group) {
    for (const auto& [g, n] : kGroups) {
        if (g == group) {
            return n;
        }
    }
    return "node-emission";
}

std::optional<ReactiveGroup> reactiveGroupFromName(std::string_view name) {
    for (const auto& [g, n] : kGroups) {
        if (name == n) {
            return g;
        }
    }
    return std::nullopt;
}

std::span<const ReactiveGroup> allReactiveGroups() {
    return kAllGroups;
}

const ReactiveTarget* ReactiveCatalog::find(std::string_view path) const {
    const auto it = std::find_if(targets.begin(), targets.end(), [&](const ReactiveTarget& t) { return t.path == path; });
    return it == targets.end() ? nullptr : &*it;
}

std::vector<const ReactiveTarget*> ReactiveCatalog::inGroup(ReactiveGroup group) const {
    std::vector<const ReactiveTarget*> out;
    for (const ReactiveTarget& t : targets) {
        if (t.group == group) {
            out.push_back(&t);
        }
    }
    return out;
}

const ReactiveHero* ReactiveCatalog::hero(std::string_view name) const {
    const auto it = std::find_if(heroes.begin(), heroes.end(), [&](const ReactiveHero& h) { return h.name == name; });
    return it == heroes.end() ? nullptr : &*it;
}

json ReactiveCatalog::toJson() const {
    json list = json::array();
    for (const ReactiveTarget& t : targets) {
        json o{{"path", t.path},
               {"component", t.component},
               {"group", reactiveGroupName(t.group)},
               {"kind", reactiveKindName(t.kind)},
               {"level", reactiveLevelName(t.level)},
               {"owner", t.owner},
               {"label", t.label},
               {"base", t.base},
               {"neutral", t.neutral},
               {"safeMin", t.safeMin},
               {"safeMax", t.safeMax},
               {"op", t.op == ModOp::Add ? "add" : "multiply"}};
        if (!t.hero.empty()) {
            o["hero"] = t.hero;
        }
        if (t.position) {
            o["position"] = {t.position->x, t.position->y, t.position->z};
        }
        if (t.size > 0.0f) {
            o["size"] = t.size;
        }
        if (t.emission > 0.0f) {
            o["emission"] = t.emission;
            o["programLit"] = t.programLit;
        }
        if (t.global) {
            o["global"] = true;
        }
        if (t.scripted) {
            o["scripted"] = true;
        }
        if (!t.sharedBy.empty()) {
            o["sharedBy"] = t.sharedBy;
        }
        if (!t.field.empty()) {
            o["field"] = t.field;
            o["fieldTriggered"] = t.fieldTriggered;
        }
        if (t.keyed) {
            o["keyed"] = true;
        }
        if (!t.drivenBy.empty()) {
            o["drivenBy"] = t.drivenBy;
        }
        if (!t.plannedBy.empty()) {
            o["plannedBy"] = t.plannedBy;
        }
        if (!t.hazards.empty()) {
            json hazards = json::array();
            for (const params::liveness::Finding& f : t.hazards) {
                hazards.push_back({{"rule", f.rule}, {"reason", f.reason}});
            }
            o["hazards"] = std::move(hazards);
        }
        list.push_back(std::move(o));
    }
    json heroesJson = json::array();
    for (const ReactiveHero& h : heroes) {
        heroesJson.push_back({{"name", h.name},
                              {"position", {h.position.x, h.position.y, h.position.z}},
                              {"radius", h.radius},
                              {"importance", h.importance},
                              {"members", h.members}});
    }
    return {{"targets", std::move(list)}, {"heroes", std::move(heroesJson)}, {"excluded", excluded}};
}

ReactiveCatalog buildReactiveCatalog(const ReactiveInputs& in) {
    ReactiveCatalog out;
    if (in.params == nullptr) {
        return out;
    }
    const SceneIndex index = indexScene(in.composition);
    out.heroes = index.heroes;

    bool anyLayerEmits = false;
    if (in.composition != nullptr) {
        for (const auto& node : in.composition->nodes()) {
            if (node != nullptr && node->kind == scene::NodeKind::Terrain) {
                for (const world::ScatterLayer& layer : node->ecology.layers) {
                    anyLayerEmits = anyLayerEmits || layerEmits(*in.composition, layer);
                }
            }
        }
    }
    const bool windOn = [&] {
        const params::IParameter* enabled = in.params->find("scene/wind/enabled");
        return enabled == nullptr || enabled->baseComponent(0) >= 0.5f;
    }();

    std::vector<const params::IParameter*> ordered;
    for (const params::IParameter* p : in.params->ordered()) {
        if (p != nullptr && p->flags().modulatable && p->componentCount() == 1 &&
            p->kind() != params::ParamKind::Bool && p->kind() != params::ParamKind::Int) {
            ordered.push_back(p);
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const params::IParameter* a, const params::IParameter* b) {
        return a->path() < b->path();
    });

    for (const params::IParameter* p : ordered) {
        const std::string& path = p->path();
        std::optional<Candidate> c = classify(path, *p, index, anyLayerEmits);
        if (!c) {
            continue;
        }
        if (c->group == ReactiveGroup::Wind && !windOn) {
            c->excluded = "the scene's wind is switched off";
        }
        // The effect list decides an effect target's level and hero: a world effect is the world's; an
        // effect on a node answers with the node's hero.
        if (c->group == ReactiveGroup::Effect && c->excluded.empty()) {
            const std::vector<std::string_view> seg = split(path);
            const auto e = std::find_if(in.effects.begin(), in.effects.end(),
                                        [&](const world::EffectInstance& x) { return x.id == seg[1]; });
            if (e == in.effects.end()) {
                continue;
            }
            if (!e->enabled) {
                c->excluded = fmt::format("effect '{}' is switched off", e->id);
            } else if (e->owner.isWorld()) {
                c->level = ReactiveLevel::Macro;
                c->owner = e->id;
                c->label = fmt::format("{} {}", e->name.empty() ? e->id : e->name, split(path).back());
                c->lo = 0.85f; // a world effect is part of the look: the lead may lift it, not strobe it
                c->hi = 1.2f;
            } else {
                c->heroNode = e->owner.name;
                c->label = fmt::format("{} {} ({})", e->name.empty() ? e->id : e->name, split(path).back(), e->owner.name);
            }
        }
        // The liveness registry has the last word (ADR-902): a dead target is never offered, and neither
        // is a phase rate, whose pattern jumps when it moves. Other hazards stay with the entry.
        std::vector<params::liveness::Finding> hazards;
        if (c->excluded.empty() && in.liveness != nullptr) {
            for (const params::liveness::Finding& f :
                 params::liveness::Registry::standard().checkTarget(path, -1, *in.liveness)) {
                if (f.verdict == params::liveness::Verdict::Dead) {
                    c->excluded = fmt::format("dead [{}]: {}", f.rule, f.reason);
                    break;
                }
                if (f.rule == "phase-rate") {
                    c->excluded = fmt::format("a phase rate [phase-rate]: {}", f.reason);
                    break;
                }
                hazards.push_back(f);
            }
        }
        if (!c->excluded.empty()) {
            out.excluded.push_back(fmt::format("{}: {}", path, c->excluded));
            continue;
        }
        ReactiveTarget& t = out.targets.emplace_back();
        t.hazards = std::move(hazards);
        t.path = path;
        t.component = -1;
        t.group = c->group;
        t.kind = c->kind;
        t.level = c->level;
        t.owner = c->owner;
        t.label = c->label;
        t.op = c->op;
        t.base = p->baseComponent(0);
        t.global = c->global;
        t.position = c->position;
        t.field = c->field;
        t.fieldTriggered = c->fieldTriggered;
        t.sharedBy = c->sharedBy;
        t.emission = c->emission;
        t.programLit = c->programLit;
        t.scripted = (!c->heroNode.empty() && index.scripted.contains(c->heroNode)) || index.scripted.contains(c->owner);
        t.hero = c->heroNode.empty() ? std::string() : index.hero(c->heroNode);
        // A practical light has no node to inherit a hero from: it answers for the hero it stands beside.
        if (t.hero.empty() && t.group == ReactiveGroup::Light && !t.global && t.position) {
            float best = 0.0f;
            for (const ReactiveHero& h : index.heroes) {
                const float d = glm::length(glm::vec2(t.position->x - h.position.x, t.position->z - h.position.z));
                if (d <= std::max(h.radius * 2.0f, h.radius + 3.0f) && (t.hero.empty() || d < best)) {
                    t.hero = h.name;
                    best = d;
                }
            }
        }
        if (t.group == ReactiveGroup::NodeEmission && !t.hero.empty()) {
            t.group = ReactiveGroup::HeroEmission;
        }
        t.size = c->size > 0.0f ? c->size : heroRadius(index, t.hero);
        // Where it rests, and how far a route may take it.
        t.neutral = t.group == ReactiveGroup::ScatterHue ? 0.0f : t.base;
        if (c->absolute) {
            t.safeMin = c->lo;
            t.safeMax = c->hi;
        } else if (c->op == ModOp::Multiply) {
            t.safeMin = t.base * c->lo;
            t.safeMax = t.base * c->hi;
        } else {
            t.safeMin = t.base + c->lo;
            t.safeMax = t.base + c->hi;
        }
        t.safeMin = std::max(t.safeMin, p->hardMin(0));
        t.safeMax = std::min(t.safeMax, p->hardMax(0));
        t.keyed = std::find(in.keyedTargets.begin(), in.keyedTargets.end(), path) != in.keyedTargets.end();
        for (const params::ModRoute& r : in.routes) {
            if (r.enabled && r.target == path) {
                addTo(r.planItem.empty() ? t.drivenBy : t.plannedBy, r.planItem.empty() ? r.source : r.planItem);
            }
        }
    }
    // Deterministic order: by group, then owner, then path.
    std::stable_sort(out.targets.begin(), out.targets.end(), [](const ReactiveTarget& a, const ReactiveTarget& b) {
        if (a.group != b.group) {
            return a.group < b.group;
        }
        if (a.owner != b.owner) {
            return a.owner < b.owner;
        }
        return a.path < b.path;
    });
    return out;
}

} // namespace avgen::directing
