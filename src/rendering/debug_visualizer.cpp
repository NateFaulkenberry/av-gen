#include "rendering/debug_visualizer.hpp"

#include "spatial/field.hpp"

#include <array>

#include <algorithm>
#include <cmath>
#include <limits>

namespace avgen::rendering {

namespace {

constexpr glm::vec4 kBoundsColour{0.35f, 0.75f, 1.0f, 0.7f};
constexpr glm::vec4 kNormalColour{0.4f, 1.0f, 0.55f, 0.8f};
constexpr glm::vec4 kSplineColour{1.0f, 0.65f, 0.85f, 0.9f};
constexpr glm::vec4 kFieldColour{1.0f, 0.55f, 0.3f, 0.75f};
constexpr glm::vec4 kSdfColour{0.5f, 1.0f, 0.9f, 0.7f};
constexpr glm::vec4 kOriginColour{1.0f, 1.0f, 1.0f, 0.9f};
constexpr glm::vec4 kTrailColour{1.0f, 0.45f, 0.15f, 0.9f};
constexpr glm::vec4 kFrustumColour{0.85f, 0.85f, 0.4f, 0.7f};
constexpr glm::vec4 kJointColour{1.0f, 0.85f, 0.3f, 0.95f};
constexpr glm::vec4 kBoneColour{0.3f, 0.9f, 1.0f, 0.85f};
// ADR-260: the emitter disc and the column's own axis.
constexpr glm::vec4 kBeamColour{0.25f, 0.95f, 1.0f, 0.95f};
// Where the column ends, dimmer -- "it stops here" is a softer fact than "it starts here", because
// the particles fade over their life rather than switching off.
constexpr glm::vec4 kBeamEndColour{0.25f, 0.95f, 1.0f, 0.35f};

// Blue to red across a normalised value.
glm::vec4 heat(float t, float alpha) {
    t = std::clamp(t, 0.0f, 1.0f);
    return glm::vec4(t, 0.35f * (1.0f - std::abs(t - 0.5f) * 2.0f) + 0.2f, 1.0f - t, alpha);
}

// The eight corners of a camera's frustum, through the inverse of the matrix the camera would draw
// with. NDC z runs 0..1 here because that is what the projection produces (WebGPU/Metal); using the
// OpenGL -1..1 cube would put the near plane halfway down the volume and the box would be a
// plausible, wrong shape.
void drawFrustum(DebugDraw& draw, const scene::Camera& camera, float aspect) {
    const glm::mat4 inverse = glm::inverse(camera.projection(std::max(1e-3f, aspect)) * camera.view());
    std::array<glm::vec3, 8> corner{};
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 ndc((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : 0.0f, 1.0f);
        const glm::vec4 world = inverse * ndc;
        if (std::abs(world.w) < 1e-8f || !std::isfinite(world.w)) {
            return; // a degenerate projection draws nothing rather than a box at infinity
        }
        corner[static_cast<std::size_t>(i)] = glm::vec3(world) / world.w;
    }
    // Near face 0,1,3,2 -- the bit pattern makes 0-1 and 2-3 the horizontal edges.
    const int nearLoop[4] = {0, 1, 3, 2};
    const int farLoop[4] = {4, 5, 7, 6};
    for (int i = 0; i < 4; ++i) {
        draw.line(corner[static_cast<std::size_t>(nearLoop[i])], corner[static_cast<std::size_t>(nearLoop[(i + 1) % 4])],
                  kFrustumColour);
        draw.line(corner[static_cast<std::size_t>(farLoop[i])], corner[static_cast<std::size_t>(farLoop[(i + 1) % 4])],
                  kFrustumColour);
        draw.line(corner[static_cast<std::size_t>(nearLoop[i])], corner[static_cast<std::size_t>(farLoop[i])],
                  kFrustumColour);
    }
    // The basis, so "which way is the camera facing" is answerable without reading the box.
    const glm::mat4 view = camera.view();
    const glm::mat3 basis = glm::transpose(glm::mat3(view));
    glm::mat4 frame(basis);
    frame[3] = glm::vec4(camera.position, 1.0f);
    draw.axis(frame, glm::length(corner[0] - camera.position) * 0.5f);
}

glm::vec4 idColour(int id, float alpha) {
    const auto h = static_cast<std::uint32_t>(id) * 2654435761u;
    return glm::vec4(static_cast<float>((h >> 16) & 0xFF) / 255.0f, static_cast<float>((h >> 8) & 0xFF) / 255.0f,
                     static_cast<float>(h & 0xFF) / 255.0f, alpha);
}

} // namespace

void buildDebugGeometry(DebugDraw& draw, const scene::Scene& scene, const DebugViewOptions& options, double time,
                        const TransformHistory* history) {
    int budget = std::max(0, options.maxPoints);

    if (options.worldAxes) {
        // Sized from the distance to the camera so it stays legible in a two-metre room and in a
        // kilometre of terrain, and floored so it never vanishes when the camera sits on the origin.
        const float span = std::max(1.0f, glm::length(scene.camera.position) * 0.25f);
        draw.axis(glm::mat4(1.0f), span);
        draw.point(glm::vec3(0.0f), options.pointSize * 0.12f, kOriginColour);
    }

    if (options.beams) {
        // The same integration `tests/unit/test_beam_lab.cpp` asserts against, spelled here so the
        // overlay and the test cannot disagree about where the column ends.
        const auto reach = [](const scene::ParticleSystem& ps) {
            const float dt = 1.0f / 120.0f;
            float v = ps.speedMin;
            float travelled = 0.0f;
            for (float t = 0.0f; t < ps.lifetimeMin; t += dt) {
                v += -ps.gravity.y * dt;
                v *= std::max(0.0f, 1.0f - ps.drag * dt);
                travelled += v * dt;
            }
            return travelled;
        };
        for (const scene::ParticleSystem& ps : scene.particles) {
            if (!ps.enabled) {
                continue;
            }
            const glm::vec3 axis = glm::normalize(ps.direction + glm::vec3(1e-5f, 0.0f, 0.0f));
            const glm::vec3 bottom = ps.position + axis * reach(ps);
            draw.circle(ps.position, axis, ps.extent.x, kBeamColour);
            draw.line(ps.position, bottom, kBeamColour);
            // Where it stops. Dimmer, because "the column ends here" is a softer fact than "the
            // column starts here" -- the particles fade over their life rather than switching off.
            draw.circle(bottom, axis, ps.extent.x, kBeamEndColour);
            draw.point(ps.position, options.pointSize * 0.12f, kBeamColour);
        }
    }

    if (options.frustum) {
        drawFrustum(draw, scene.camera, options.frustumAspect);
    }

    if (options.transformTrail && history != nullptr) {
        const std::vector<glm::vec3> points = history->path();
        if (points.size() >= 2) {
            draw.polyline(points, kTrailColour);
        }
        for (const glm::vec3& p : points) {
            draw.point(p, options.pointSize * 0.5f, kTrailColour);
        }
    }

    for (std::size_t entityIndex = 0; entityIndex < scene.entities.size(); ++entityIndex) {
        const scene::Entity& entity = scene.entities[entityIndex];
        if (!entity.visible || (!options.selectedEntity.empty() && entity.name != options.selectedEntity) ||
            entity.mesh >= scene.meshes.size()) {
            continue;
        }
        // What the frame submits is what survived the camera cull. This is the *set*, not the
        // triangles: the arm answers "was this object handed to the GPU", which is the question a
        // missing object raises, and it is a claim the visualiser can make honestly.
        if (options.submittedOnly && entity.cameraCulled) {
            continue;
        }
        const auto [meshLo, meshHi] = scene.meshes[entity.mesh].bounds();
        const glm::mat4 model = entity.transform.matrix();
        glm::vec3 worldLo(std::numeric_limits<float>::max());
        glm::vec3 worldHi(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? meshHi.x : meshLo.x,
                                  (corner & 2) ? meshHi.y : meshLo.y,
                                  (corner & 4) ? meshHi.z : meshLo.z);
            const glm::vec3 world = glm::vec3(model * glm::vec4(local, 1.0f));
            worldLo = glm::min(worldLo, world);
            worldHi = glm::max(worldHi, world);
        }
        // The id the identifier target writes for this entity, so the overlay and the `Ids`
        // auxiliary view agree on which object is which. Colouring by loop index instead would be a
        // second numbering, and two numberings is how a pick used to select the wrong tree.
        const std::uint32_t pickId = scene::packPickId(scene::PickSpace::Entity, entityIndex);
        const glm::vec4 culledColour(1.0f, 0.2f, 0.1f, 0.9f);
        const glm::vec4 boundsColour = options.entityIds ? idColour(static_cast<int>(pickId), 0.9f)
                                       : entity.cameraCulled ? culledColour
                                                             : kBoundsColour;
        if (options.entityBounds || options.entityIds) {
            draw.box(worldLo, worldHi, boundsColour);
        }
        if (options.skeletons && entity.rig < scene.rigs.size()) {
            const scene::SkinnedRig& rig = scene.rigs[entity.rig];
            // The model-space matrices the pose produced. Empty until the rig has been evaluated,
            // which is the honest state to draw nothing for: a skeleton drawn from a rest pose that
            // the frame is not using would be a second answer to "where are the joints".
            const std::vector<glm::mat4>& jointModel = rig.scratchModel;
            const std::size_t joints = std::min(jointModel.size(), rig.skeleton.joints.size());
            for (std::size_t j = 0; j < joints; ++j) {
                const glm::vec3 here = glm::vec3(model * jointModel[j][3]);
                draw.point(here, options.pointSize * 0.6f, kJointColour);
                const int parent = rig.skeleton.joints[j].parent;
                if (parent >= 0 && static_cast<std::size_t>(parent) < joints) {
                    const glm::vec3 up = glm::vec3(model * jointModel[static_cast<std::size_t>(parent)][3]);
                    draw.line(up, here, kBoneColour);
                }
            }
        }
        if (options.entityOrigins && budget > 0) {
            draw.point(glm::vec3(model[3]), options.pointSize * 0.08f,
                       entity.cameraCulled ? culledColour : glm::vec4(1.0f, 0.9f, 0.2f, 0.9f));
            draw.axis(model, std::max(0.25f, glm::length(worldHi - worldLo) * 0.2f));
            --budget;
        }
    }

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
