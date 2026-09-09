#include "rendering/debug_visualizer.hpp"

#include "spatial/field.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::rendering {

namespace {

constexpr glm::vec4 kBoundsColour{0.35f, 0.75f, 1.0f, 0.7f};
constexpr glm::vec4 kNormalColour{0.4f, 1.0f, 0.55f, 0.8f};
constexpr glm::vec4 kSplineColour{1.0f, 0.65f, 0.85f, 0.9f};
constexpr glm::vec4 kFieldColour{1.0f, 0.55f, 0.3f, 0.75f};
constexpr glm::vec4 kSdfColour{0.5f, 1.0f, 0.9f, 0.7f};

// Blue to red across a normalised value.
glm::vec4 heat(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::vec4(t, 0.35f * (1.0f - std::abs(t - 0.5f) * 2.0f) + 0.2f, 1.0f - t, alpha);
}

glm::vec4 idColour(int id, float alpha) {
    const auto h = static_cast<std::uint32_t>(id) * 2654435761u;
    return glm::vec4(static_cast<float>((h >> 16) & 0xFF) / 255.0f, static_cast<float>((h >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(h & 0xFF) / 255.0f, alpha);
}

} // namespace

void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time) {
    int budget = std::max(0, options.maxPoints);

    for (const scene::ProceduralGeometry& pg : scene.procedurals) {
        if (!pg.visible) {
            continue;
        }
        if (options.bounds) {
            draw.box(pg.boundsMin, pg.boundsMax, kBoundsColour);
        }
        const bool wantPoints = options.points || options.density || options.instanceIds || !options.attribute.empty();
        if (!wantPoints && !options.normals) {
            continue;
        }
        // Attribute colouring needs the cloud; instance records carry the rest.
        const spatial::PointCloud& cloud = pg.cloud;
        const bool haveCloud = cloud.count() == pg.instances.size();
        const spatial::AttributeBuffer* attribute =
            !options.attribute.empty() && haveCloud ? cloud.attributes.find(options.attribute) : nullptr;
        spatial::AttributeStats stats;
        if (attribute != nullptr) {
            stats = spatial::attributeStats(*attribute);
        }
        for (std::size_t i = 0; i < pg.instances.size() && budget > 0; ++i) {
            const scene::InstanceRecord& record = pg.instances[i];
            const glm::vec3 position(record.position);
            glm::vec4 colour(0.85f, 0.9f, 1.0f, 0.9f);
            if (attribute != nullptr) {
                const glm::vec4 value = spatial::readAsVec4(*attribute, i);
                const float span = stats.max.x - stats.min.x;
                colour = heat(span > 1e-6f ? (value.x - stats.min.x) / span : 0.5f, 0.9f);
            } else if (options.density) {
                colour = heat(std::clamp(record.position.w, 0.0f, 1.0f), 0.9f);
            } else if (options.instanceIds) {
                colour = idColour(static_cast<int>(record.color.a), 0.9f);
            }
            if (wantPoints) {
                draw.point(position, options.pointSize * 0.05f, colour);
                --budget;
            }
            if (options.normals) {
                const glm::quat rotation(record.rotation.w, record.rotation.x, record.rotation.y, record.rotation.z);
                const float length = std::max(0.2f, glm::length(glm::vec3(record.scale)) * 0.5f);
                draw.line(position, position + rotation * glm::vec3(0.0f, 1.0f, 0.0f) * length, kNormalColour);
            }
        }
    }

    if (options.splines) {
        for (const spatial::Spline& spline : scene.splines.splines) {
            const auto samples = spline.samples(96);
            std::vector<glm::vec3> path;
            path.reserve(samples.size());
            for (const spatial::SplineSample& s : samples) {
                path.push_back(s.position);
            }
            draw.polyline(path, kSplineColour, spline.closed);
            // Frames every eighth sample so the twist is visible.
            for (std::size_t i = 0; i < samples.size(); i += 8) {
                const spatial::SplineSample& s = samples[i];
                const float length = std::max(0.2f, spline.length() * 0.02f);
                draw.line(s.position, s.position + s.normal * length, glm::vec4(0.4f, 1.0f, 0.6f, 0.8f));
                draw.line(s.position, s.position + s.binormal * length, glm::vec4(1.0f, 0.5f, 0.5f, 0.8f));
            }
        }
    }

    if (options.fields || options.fieldVectors) {
        for (const spatial::FieldSpec& field : scene.fields.fields) {
            if (!field.enabled) {
                continue;
            }
            const glm::mat4 toWorld = field.localToWorld();
            const glm::vec3 origin = glm::vec3(toWorld[3]);
            if (options.fields) {
                draw.axis(toWorld, std::max(1.0f, field.falloff.outer * 0.1f));
                if (field.falloff.outer > 0.0f) {
                    draw.circle(origin, glm::vec3(0.0f, 1.0f, 0.0f), field.falloff.outer, kFieldColour, 48);
                    draw.circle(origin, glm::vec3(1.0f, 0.0f, 0.0f), field.falloff.outer, kFieldColour, 48);
                }
                if (field.falloff.inner > 0.0f) {
                    draw.circle(origin, glm::vec3(0.0f, 1.0f, 0.0f), field.falloff.inner,
                                glm::vec4(kFieldColour.r, kFieldColour.g, kFieldColour.b, 0.35f), 32);
                }
            }
            if (!options.fieldVectors) {
                continue;
            }
            const int grid = std::clamp(options.fieldGrid, 2, 24);
            const float extent = field.falloff.outer > 0.0f ? field.falloff.outer : 10.0f;
            const float step = 2.0f * extent / static_cast<float>(grid - 1);
            for (int z = 0; z < grid && budget > 0; ++z) {
                for (int y = 0; y < grid && budget > 0; ++y) {
                    for (int x = 0; x < grid && budget > 0; ++x) {
                        const glm::vec3 p = origin + glm::vec3(-extent + step * static_cast<float>(x),
                                                               -extent + step * static_cast<float>(y),
                                                               -extent + step * static_cast<float>(z));
                        const glm::vec3 v = spatial::sampleVector(field, p, time, &scene.fields);
                        const float length = glm::length(v);
                        if (length < 1e-4f) {
                            continue;
                        }
                        draw.arrow(p, v, std::min(step * 0.8f, length * step * 0.4f),
                                   heat(std::clamp(length, 0.0f, 1.0f), 0.8f));
                        --budget;
                    }
                }
            }
        }
    }

    if (options.sdfSlice) {
        for (const scene::SdfObject& object : scene.sdfs) {
            if (!object.visible) {
                continue;
            }
            draw.box(object.transform.matrix(), (object.boundsMax - object.boundsMin) * 0.5f, kSdfColour);
            // Iso-lines on a horizontal slice: sample a grid and mark near-surface cells.
            const int grid = 48;
            const glm::vec3 size = object.boundsMax - object.boundsMin;
            const float cell = std::max(size.x, size.z) / static_cast<float>(grid);
            for (int z = 0; z < grid && budget > 0; ++z) {
                for (int x = 0; x < grid && budget > 0; ++x) {
                    const glm::vec3 local(object.boundsMin.x + size.x * (static_cast<float>(x) + 0.5f) / grid,
                                          options.sliceHeight,
                                          object.boundsMin.z + size.z * (static_cast<float>(z) + 0.5f) / grid);
                    const float d = object.tree.evaluate(local, time, &scene.fields);
                    if (std::abs(d) > cell) {
                        continue;
                    }
                    const glm::vec3 world = glm::vec3(object.transform.matrix() * glm::vec4(local, 1.0f));
                    draw.point(world, cell, glm::vec4(kSdfColour.r, kSdfColour.g, kSdfColour.b, 0.9f));
                    --budget;
                }
            }
        }
    }
}

} // namespace avgen::rendering
