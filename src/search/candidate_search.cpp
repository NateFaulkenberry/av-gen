#include "search/candidate_search.hpp"

#include "scene/struct_hash.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace avgen::search {
namespace {

using json = nlohmann::json;
using scene::detail::StructHash;

// ---- Sobol direction numbers -----------------------------------------------------------------
//
// Joe & Kuo's primitive polynomials and initial direction numbers, for dimensions 2..21. Dimension
// 1 is the van der Corput sequence and needs no table. These are mathematical constants, not tuning:
// the polynomial degree `s`, its coefficient bitmask `a`, and the first `s` odd initial values `m`.
//
// 21 dimensions is a deliberate ceiling rather than an arbitrary one. A schema wider than this has
// almost certainly not been pruned to the parameters that matter (the mushroom schema is 18), and
// Sobol's projections degrade with dimension index besides -- so the honest failure is an error at
// schema validation, not a silently worse sample in the last few axes.
struct SobolPoly {
    std::uint32_t s;
    std::uint32_t a;
    std::array<std::uint32_t, 8> m;
};
constexpr std::array<SobolPoly, 20> kSobol{{
    {1, 0, {1}},                        {2, 1, {1, 3}},
    {3, 1, {1, 3, 1}},                  {3, 2, {1, 1, 1}},
    {4, 1, {1, 1, 3, 3}},               {4, 4, {1, 3, 5, 13}},
    {5, 2, {1, 1, 5, 5, 17}},           {5, 4, {1, 1, 5, 5, 5}},
    {5, 7, {1, 1, 7, 11, 19}},          {5, 11, {1, 1, 5, 1, 1}},
    {5, 13, {1, 1, 1, 3, 11}},          {5, 14, {1, 3, 5, 5, 31}},
    {6, 1, {1, 3, 3, 9, 7, 49}},        {6, 13, {1, 1, 1, 15, 21, 21}},
    {6, 16, {1, 3, 1, 13, 27, 49}},     {6, 19, {1, 1, 1, 15, 7, 5}},
    {6, 22, {1, 3, 1, 15, 13, 25}},     {6, 25, {1, 1, 5, 5, 19, 61}},
    {7, 1, {1, 3, 7, 11, 23, 15, 103}}, {7, 4, {1, 3, 7, 13, 25, 43, 69}},
}};
constexpr std::uint32_t kMaxDimensions = 1 + static_cast<std::uint32_t>(kSobol.size());
constexpr std::uint32_t kBits = 32;

// The 32 direction numbers of one dimension, built by the standard recurrence.
std::array<std::uint32_t, kBits> directions(std::uint32_t dimension) {
    std::array<std::uint32_t, kBits> v{};
    if (dimension == 0) {
        for (std::uint32_t i = 0; i < kBits; ++i) {
            v[i] = 1u << (kBits - 1 - i);
        }
        return v;
    }
    const SobolPoly& p = kSobol[dimension - 1];
    for (std::uint32_t i = 0; i < p.s && i < kBits; ++i) {
        v[i] = p.m[i] << (kBits - 1 - i);
    }
    for (std::uint32_t i = p.s; i < kBits; ++i) {
        v[i] = v[i - p.s] ^ (v[i - p.s] >> p.s);
        for (std::uint32_t k = 1; k < p.s; ++k) {
            if ((p.a >> (p.s - 1 - k)) & 1u) {
                v[i] ^= v[i - k];
            }
        }
    }
    return v;
}

// A fixed digital shift per dimension. Fixed *forever*: it is part of what `sampleAt` means, so
// changing it would silently re-point every stored candidate index at a different individual. It
// exists because the unscrambled sequence starts at the origin, and "every parameter at its minimum"
// is a degenerate corner nobody wants as candidate 0.
std::uint32_t scramble(std::uint32_t dimension) {
    std::uint64_t h = 0x9e3779b97f4a7c15ULL ^ (static_cast<std::uint64_t>(dimension) * 0xbf58476d1ce4e5b9ULL);
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return static_cast<std::uint32_t>(h ^ (h >> 31));
}

// Point `index` of dimension `dimension`, as a 32-bit fixed-point fraction. Computed directly from
// the index via its Gray code, so points are independent and parallelisable -- the property the
// whole identity scheme rests on.
std::uint32_t sobolPoint(std::uint32_t dimension, std::uint32_t index) {
    const std::array<std::uint32_t, kBits> v = directions(dimension);
    std::uint32_t x = scramble(dimension);
    std::uint32_t gray = index ^ (index >> 1);
    for (std::uint32_t bit = 0; gray != 0; ++bit, gray >>= 1) {
        if (gray & 1u) {
            x ^= v[bit];
        }
    }
    return x;
}

float smootherstep(float t) { return t * t * (3.0f - 2.0f * t); }

} // namespace

// ---------------------------------------------------------------------------------------------

std::optional<std::size_t> ParameterSchema::indexOf(std::string_view name) const {
    for (std::size_t i = 0; i < specs_.size(); ++i) {
        if (specs_[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

Result<void> ParameterSchema::validate() const {
    if (specs_.empty()) {
        return fail("parameter schema is empty");
    }
    if (specs_.size() > kMaxDimensions) {
        return fail("parameter schema has {} axes; the sampler carries direction numbers for {}. "
                    "A schema this wide has probably not been pruned to the parameters that matter",
                    specs_.size(), kMaxDimensions);
    }
    for (std::size_t i = 0; i < specs_.size(); ++i) {
        const ParameterSpec& s = specs_[i];
        if (s.name.empty()) {
            return fail("parameter {} has no name", i);
        }
        if (!std::isfinite(s.min) || !std::isfinite(s.max)) {
            return fail("parameter '{}': bounds must be finite", s.name);
        }
        if (!(s.max > s.min)) {
            return fail("parameter '{}': max ({}) must exceed min ({})", s.name, s.max, s.min);
        }
        for (std::size_t k = 0; k < i; ++k) {
            if (specs_[k].name == s.name) {
                return fail("parameter '{}' is declared twice", s.name);
            }
        }
    }
    return {};
}

Result<void> GeneratorSchema::validate() const {
    if (generatorName.empty()) {
        return fail("generator schema has no name");
    }
    if (generatorVersion == 0) {
        return fail("generator '{}': version must be >= 1", generatorName);
    }
    if (auto ok = parameters.validate(); !ok) {
        return fail("generator '{}': {}", generatorName, ok.error().message);
    }
    if (featureNames.empty()) {
        return fail("generator '{}': needs at least one feature axis to select for diversity on",
                    generatorName);
    }
    return {};
}

std::uint64_t GeneratorSchema::hash() const {
    StructHash h;
    h.str(generatorName);
    h.u32(generatorVersion);
    // The order is hashed because the order is load-bearing: a reordered schema is a different
    // sample even though every range is unchanged.
    h.u64(parameters.size());
    for (const ParameterSpec& s : parameters.specs()) {
        h.str(s.name);
        h.f32(s.min);
        h.f32(s.max);
        h.boolean(s.integral);
    }
    h.u64(featureNames.size());
    for (const std::string& n : featureNames) {
        h.str(n);
    }
    return h.value();
}

Parameters sampleAt(const ParameterSchema& schema, std::uint32_t index) {
    Parameters out;
    out.reserve(schema.size());
    for (std::size_t d = 0; d < schema.size(); ++d) {
        const ParameterSpec& spec = schema.at(d);
        const std::uint32_t fixed = sobolPoint(static_cast<std::uint32_t>(d) % kMaxDimensions, index);
        const float u = static_cast<float>(fixed) * (1.0f / 4294967296.0f); // [0, 1)
        float v = spec.min + (spec.max - spec.min) * u;
        if (spec.integral) {
            // Round rather than truncate so both endpoints are reachable and equally likely.
            v = std::round(v);
            v = std::clamp(v, std::ceil(spec.min), std::floor(spec.max));
        }
        out.push_back(v);
    }
    return out;
}

// ---------------------------------------------------------------------------------------------

Result<void> ScoreBand::validate() const {
    if (!std::isfinite(lowEdge) || !std::isfinite(lowPlateau) || !std::isfinite(highPlateau) ||
        !std::isfinite(highEdge)) {
        return fail("score band: all four edges must be finite -- an infinite edge is a monotone "
                    "component, which is the shape this type exists to prevent (ADR-172)");
    }
    if (!(lowEdge <= lowPlateau && lowPlateau <= highPlateau && highPlateau <= highEdge)) {
        return fail("score band: needs lowEdge <= lowPlateau <= highPlateau <= highEdge, got "
                    "{} {} {} {}", lowEdge, lowPlateau, highPlateau, highEdge);
    }
    return {};
}

float ScoreBand::operator()(float x) const {
    if (!std::isfinite(x) || x <= lowEdge || x >= highEdge) {
        return 0.0f;
    }
    if (x >= lowPlateau && x <= highPlateau) {
        return 1.0f;
    }
    if (x < lowPlateau) {
        const float d = lowPlateau - lowEdge;
        return d > 0.0f ? smootherstep(std::clamp((x - lowEdge) / d, 0.0f, 1.0f)) : 1.0f;
    }
    const float d = highEdge - highPlateau;
    return d > 0.0f ? smootherstep(std::clamp((highEdge - x) / d, 0.0f, 1.0f)) : 1.0f;
}

float Score::overall() const {
    float sum = 0.0f;
    float weights = 0.0f;
    for (const ScoreComponent& c : components) {
        sum += c.score * c.weight;
        weights += c.weight;
    }
    float value = weights > 0.0f ? sum / weights : 0.0f;
    for (const Penalty& p : penalties) {
        value -= p.amount;
    }
    return value; // deliberately unclamped -- see the header
}

const ScoreComponent* Score::find(std::string_view name) const {
    for (const ScoreComponent& c : components) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------

std::optional<Rejection> meshHygiene(const scene::MeshData& mesh, const HygieneLimits& limits) {
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return Rejection{"empty", "mesh has no vertices or no indices"};
    }
    if (mesh.indices.size() % 3 != 0) {
        return Rejection{"indices", fmt::format("{} indices is not a multiple of 3", mesh.indices.size())};
    }
    const auto triangles = static_cast<std::uint32_t>(mesh.indices.size() / 3);
    if (triangles > limits.maxTriangles) {
        return Rejection{"triangle-budget", fmt::format("{} triangles exceeds {}", triangles, limits.maxTriangles)};
    }
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(std::numeric_limits<float>::lowest());
    for (const scene::Vertex& v : mesh.vertices) {
        if (!std::isfinite(v.position.x) || !std::isfinite(v.position.y) || !std::isfinite(v.position.z)) {
            return Rejection{"non-finite-position", "a vertex position is NaN or infinite"};
        }
        if (limits.requireFiniteNormals &&
            (!std::isfinite(v.normal.x) || !std::isfinite(v.normal.y) || !std::isfinite(v.normal.z))) {
            return Rejection{"non-finite-normal", "a vertex normal is NaN or infinite"};
        }
        lo = glm::min(lo, v.position);
        hi = glm::max(hi, v.position);
    }
    const float radius = glm::length(hi - lo) * 0.5f;
    if (!(radius >= limits.minBoundsRadius)) {
        return Rejection{"bounds-tiny", fmt::format("bounding radius {} below {}", radius, limits.minBoundsRadius)};
    }
    if (radius > limits.maxBoundsRadius) {
        return Rejection{"bounds-huge", fmt::format("bounding radius {} above {}", radius, limits.maxBoundsRadius)};
    }
    std::uint32_t degenerate = 0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
            return Rejection{"index-range", fmt::format("index out of range at triangle {}", i / 3)};
        }
        const glm::vec3& pa = mesh.vertices[a].position;
        const glm::vec3 ab = mesh.vertices[b].position - pa;
        const glm::vec3 ac = mesh.vertices[c].position - pa;
        if (glm::length(glm::cross(ab, ac)) <= 1e-12f) {
            ++degenerate;
        }
    }
    const float fraction = static_cast<float>(degenerate) / static_cast<float>(triangles);
    if (fraction > limits.maxDegenerateFraction) {
        return Rejection{"degenerate", fmt::format("{} of {} triangles ({:.1f}%) have zero area",
                                                   degenerate, triangles, fraction * 100.0f)};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------------------------

std::vector<std::size_t> selectDiverse(std::span<const Candidate> candidates, std::size_t count,
                                       float alpha) {
    std::vector<std::size_t> pool;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (!candidates[i].rejected && !candidates[i].features.empty()) {
            pool.push_back(i);
        }
    }
    if (pool.empty() || count == 0) {
        return {};
    }
    const std::size_t axes = candidates[pool.front()].features.size();
    // Normalise each axis by its observed range, so no axis dominates by unit.
    std::vector<float> lo(axes, std::numeric_limits<float>::max());
    std::vector<float> hi(axes, std::numeric_limits<float>::lowest());
    float scoreLo = std::numeric_limits<float>::max();
    float scoreHi = std::numeric_limits<float>::lowest();
    for (const std::size_t i : pool) {
        const Candidate& c = candidates[i];
        for (std::size_t a = 0; a < axes && a < c.features.size(); ++a) {
            lo[a] = std::min(lo[a], c.features[a]);
            hi[a] = std::max(hi[a], c.features[a]);
        }
        const float s = c.score.overall();
        scoreLo = std::min(scoreLo, s);
        scoreHi = std::max(scoreHi, s);
    }
    const auto norm = [&](const Candidate& c, std::size_t a) {
        const float span = hi[a] - lo[a];
        return span > 1e-9f ? (c.features[a] - lo[a]) / span : 0.0f;
    };
    const auto normScore = [&](const Candidate& c) {
        const float span = scoreHi - scoreLo;
        return span > 1e-9f ? (c.score.overall() - scoreLo) / span : 1.0f;
    };
    const auto distance = [&](const Candidate& a, const Candidate& b) {
        float sum = 0.0f;
        for (std::size_t k = 0; k < axes && k < a.features.size() && k < b.features.size(); ++k) {
            const float d = norm(a, k) - norm(b, k);
            sum += d * d;
        }
        return std::sqrt(sum);
    };

    std::vector<std::size_t> chosen;
    // Seed with the highest scorer. Ties break on the lower index, so the result is a pure function
    // of the population rather than of the order it happened to be assembled in.
    std::size_t best = pool.front();
    for (const std::size_t i : pool) {
        if (candidates[i].score.overall() > candidates[best].score.overall()) {
            best = i;
        }
    }
    chosen.push_back(best);

    const float a = std::clamp(alpha, 0.0f, 1.0f);
    // The normalising constant for a distance in a unit hypercube of `axes` dimensions, so that
    // `alpha` trades against a quantity on the same 0..1 scale as the score.
    const float maxDistance = std::sqrt(static_cast<float>(axes));
    while (chosen.size() < count && chosen.size() < pool.size()) {
        std::size_t pick = candidates.size(); // sentinel: no eligible candidate found
        float pickValue = -std::numeric_limits<float>::max();
        for (const std::size_t i : pool) {
            if (std::find(chosen.begin(), chosen.end(), i) != chosen.end()) {
                continue;
            }
            float minDistance = std::numeric_limits<float>::max();
            for (const std::size_t c : chosen) {
                minDistance = std::min(minDistance, distance(candidates[i], candidates[c]));
            }
            const float value = a * normScore(candidates[i]) +
                                (1.0f - a) * (minDistance / std::max(maxDistance, 1e-6f));
            if (value > pickValue) {
                pickValue = value;
                pick = i;
            }
        }
        if (pick == candidates.size()) {
            break;
        }
        chosen.push_back(pick);
    }
    return chosen;
}

std::vector<PreviewView> defaultViews() {
    return {
        {"front", 0.0f, 8.0f, 3.0f},
        {"three-quarter", 40.0f, 12.0f, 3.0f},
        {"side", 90.0f, 8.0f, 3.0f},
        {"rear-three-quarter", 145.0f, 12.0f, 3.2f},
        // Not optional. A structure the geometry above it encloses is invisible from every camera at
        // subject height, and `docs/visual-cookbook/bioluminescence.md` records that exact failure:
        // gills were added once and could not be seen, because the glow dome covered them.
        {"low", 25.0f, -22.0f, 2.2f},
        {"above", 200.0f, 55.0f, 3.4f},
    };
}

// ---------------------------------------------------------------------------------------------

bool GeneratedSource::matchesSample(const ParameterSchema& schema) const {
    const Parameters sampled = sampleAt(schema, index);
    return sampled == values;
}

Result<void> GeneratedSource::validateAgainst(const GeneratorSchema& schema) const {
    if (generatorName != schema.generatorName) {
        return fail("generated source names generator '{}' but this is '{}'", generatorName,
                    schema.generatorName);
    }
    if (generatorVersion != schema.generatorVersion) {
        return fail("generated source '{}' is version {} and this generator is version {}",
                    generatorName, generatorVersion, schema.generatorVersion);
    }
    // A moved schema is refused rather than silently reinterpreted: the values are positional, so a
    // reordered or resized schema makes every one of them mean something else.
    if (schemaHash != 0 && schemaHash != schema.hash()) {
        return fail("generated source '{}' was authored against schema hash {} and this generator's "
                    "is {}: the parameter space moved, so these values no longer mean what they did",
                    generatorName, schemaHash, schema.hash());
    }
    if (values.size() != schema.parameters.size()) {
        return fail("generated source '{}' carries {} values for a {}-axis schema", generatorName,
                    values.size(), schema.parameters.size());
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values[i])) {
            return fail("generated source '{}': parameter '{}' is not finite", generatorName,
                        schema.parameters.at(i).name);
        }
    }
    return {};
}

json generatedSourceToJson(const GeneratedSource& source) {
    json j;
    j["generator"] = source.generatorName;
    j["generatorVersion"] = source.generatorVersion;
    j["schemaHash"] = source.schemaHash;
    j["index"] = source.index;
    j["values"] = source.values;
    return j;
}

Result<GeneratedSource> generatedSourceFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("generated source must be a JSON object");
    }
    GeneratedSource s;
    if (!j.contains("generator") || !j.at("generator").is_string()) {
        return fail("generated source needs a string 'generator'");
    }
    s.generatorName = j.at("generator").get<std::string>();
    if (j.contains("generatorVersion")) {
        s.generatorVersion = j.at("generatorVersion").get<std::uint32_t>();
    }
    if (j.contains("schemaHash")) {
        s.schemaHash = j.at("schemaHash").get<std::uint64_t>();
    }
    if (j.contains("index")) {
        s.index = j.at("index").get<std::uint32_t>();
    }
    if (!j.contains("values") || !j.at("values").is_array()) {
        return fail("generated source '{}' needs a 'values' array", s.generatorName);
    }
    for (const json& v : j.at("values")) {
        if (!v.is_number()) {
            return fail("generated source '{}': every value must be a number", s.generatorName);
        }
        s.values.push_back(v.get<float>());
    }
    return s;
}

std::vector<std::string> parameterPaths(const GeneratorSchema& schema, std::string_view nodePrefix) {
    std::vector<std::string> paths;
    paths.reserve(schema.parameters.size());
    for (const ParameterSpec& spec : schema.parameters.specs()) {
        paths.emplace_back(std::string(nodePrefix) + "generated/" + spec.name);
    }
    return paths;
}

json candidateToJson(const GeneratorSchema& schema, const Candidate& candidate,
                     std::string_view selectionNote) {
    json j;
    j["generator"] = schema.generatorName;
    j["generatorVersion"] = schema.generatorVersion;
    j["schemaHash"] = schema.hash();
    j["index"] = candidate.index;
    json params = json::object();
    for (std::size_t i = 0; i < schema.parameters.size() && i < candidate.parameters.size(); ++i) {
        params[schema.parameters.at(i).name] = candidate.parameters[i];
    }
    j["parameters"] = std::move(params);
    if (candidate.rejected) {
        j["rejected"] = {{"rule", candidate.rejected->rule}, {"detail", candidate.rejected->detail}};
        return j;
    }
    json features = json::object();
    for (std::size_t i = 0; i < schema.featureNames.size() && i < candidate.features.size(); ++i) {
        features[schema.featureNames[i]] = candidate.features[i];
    }
    j["features"] = std::move(features);
    json scores = json::object();
    scores["overall"] = candidate.score.overall();
    json components = json::object();
    for (const ScoreComponent& c : candidate.score.components) {
        components[c.name] = {{"raw", c.raw}, {"score", c.score}, {"weight", c.weight}};
    }
    scores["components"] = std::move(components);
    if (!candidate.score.penalties.empty()) {
        json penalties = json::object();
        for (const Penalty& p : candidate.score.penalties) {
            penalties[p.name] = p.amount;
        }
        scores["penalties"] = std::move(penalties);
    }
    j["scores"] = std::move(scores);
    j["mesh"] = {{"triangles", candidate.triangles}, {"surfaceArea", candidate.surfaceArea}};
    if (!selectionNote.empty()) {
        j["selectionNote"] = std::string(selectionNote);
    }
    return j;
}

Result<Candidate> candidateFromJson(const GeneratorSchema& schema, const json& j) {
    if (!j.is_object()) {
        return fail("candidate record must be a JSON object");
    }
    if (!j.contains("index") || !j.at("index").is_number_unsigned()) {
        return fail("candidate record needs an unsigned 'index'");
    }
    // The hash is what makes a stale record announce itself rather than silently regenerating a
    // different individual under the same name. Refusing is the whole point of storing it.
    if (j.contains("schemaHash")) {
        const auto stored = j.at("schemaHash").get<std::uint64_t>();
        if (stored != schema.hash()) {
            return fail("candidate {} was recorded against schema hash {} but this generator's is "
                        "{}: the parameter space changed, so this index no longer names the same "
                        "individual",
                        j.at("index").get<std::uint32_t>(), stored, schema.hash());
        }
    }
    if (j.contains("generatorVersion")) {
        const auto v = j.at("generatorVersion").get<std::uint32_t>();
        if (v != schema.generatorVersion) {
            return fail("candidate {} was generated by version {} and this is version {}",
                        j.at("index").get<std::uint32_t>(), v, schema.generatorVersion);
        }
    }
    Candidate c;
    c.index = j.at("index").get<std::uint32_t>();
    c.parameters = sampleAt(schema.parameters, c.index);
    return c;
}

} // namespace avgen::search
