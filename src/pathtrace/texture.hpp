#pragma once

// Texture sampling for the path tracer (spec section 18).
//
// The colour-space rule is non-negotiable and comes from the glTF specification, not from taste:
// **base colour and emissive are sRGB-encoded and must be decoded to linear before lighting;
// metallic-roughness, normal and occlusion are linear data and must NOT be decoded.** Decoding a
// roughness map is not a subtle error -- it moves every roughness value and the whole material
// reads wrong, in a way that looks like a BSDF bug.
//
// The tracer does not decide which is which. `assets::gltf_loader` already tagged it at load time
// (`textureRef(..., srgb)` at gltf_loader.cpp:721-734, per the glTF spec) and the answer rides in
// `scene::TextureData::format`: `Rgba8Srgb` means decode, `Rgba8Unorm` and `Rgba32Float` mean do
// not. Respecting the format rather than guessing from the slot is what keeps the two renderers
// agreeing, and it means a scene that hand-builds a texture gets what it asked for.

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace avgen::pathtrace {

// Applies the wrap mode to a texel coordinate. Separate from sampling so it can be tested directly.
[[nodiscard]] int wrapTexel(int i, int size, scene::WrapMode mode);

// One texel, decoded to linear if and only if the format says it is sRGB-encoded. Alpha is never
// decoded -- it is coverage, not colour, and the glTF spec is explicit about that.
[[nodiscard]] glm::vec4 texelLinear(const scene::TextureData& tex, int x, int y);

// Bilinear (or nearest, if `ref.linearFilter` is false) sample at `uv`, in linear space.
//
// Filtering happens AFTER the sRGB decode, which is the correct order and the opposite of what a
// naive implementation does. Blending two sRGB-encoded bytes and then decoding is not the same
// number as decoding both and then blending, and the difference shows as darkened edges in exactly
// the high-contrast places people look.
[[nodiscard]] glm::vec4 sampleTexture(const scene::TextureData& tex, const scene::TextureRef& ref,
                                      glm::vec2 uv);

// Resolves a material texture slot against a texture table, returning `fallback` when the slot is
// unset or the id is out of range. Never silently returns black for a missing texture.
[[nodiscard]] glm::vec4 sampleSlot(std::span<const scene::TextureData> textures,
                                   const scene::TextureRef& ref, glm::vec2 uv, glm::vec4 fallback);

} // namespace avgen::pathtrace
