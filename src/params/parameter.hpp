#pragma once

// Parameter: a typed value with metadata and a unique path (ADR-011). `base` is the authored or
// UI value; `final` is base after modulation, recomputed every frame. Ranges clamp both.

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace avgen::params {

enum class ParamKind { Float, Int, Bool, Vec2, Vec3, Vec4, Color };

struct ParamFlags {
    bool exposed = true;     // shown in UI
    bool modulatable = true; // may be a modulation target
    bool serialized = true;  // saved in project files
};

// Backend-agnostic view of a parameter as N float components. This is what the modulator, the
// UI and the serializer use; the typed Parameter<T> is what scene code uses.
class IParameter {
public:
    virtual ~IParameter() = default;

    [[nodiscard]] virtual const std::string& path() const = 0;
    [[nodiscard]] virtual const std::string& label() const = 0;
    [[nodiscard]] virtual const std::string& group() const = 0;
    [[nodiscard]] virtual ParamKind kind() const = 0;
    [[nodiscard]] virtual ParamFlags flags() const = 0;
    [[nodiscard]] virtual std::size_t componentCount() const = 0;

    [[nodiscard]] virtual float baseComponent(std::size_t i) const = 0;
    virtual void setBaseComponent(std::size_t i, float v) = 0; // clamped to hard range
    [[nodiscard]] virtual float finalComponent(std::size_t i) const = 0;
    virtual void setFinalComponent(std::size_t i, float v) = 0; // clamped to hard range
    [[nodiscard]] virtual float defaultComponent(std::size_t i) const = 0;
    [[nodiscard]] virtual float hardMin(std::size_t i) const = 0;
    [[nodiscard]] virtual float hardMax(std::size_t i) const = 0;
    [[nodiscard]] virtual float softMin(std::size_t i) const = 0;
    [[nodiscard]] virtual float softMax(std::size_t i) const = 0;

    virtual void resetToDefault() = 0;
    virtual void resetFinal() = 0; // final = base
};

namespace detail {
template <typename T>
struct Components;
template <>
struct Components<float> {
    static constexpr std::size_t count = 1;
    static constexpr ParamKind kind = ParamKind::Float;
    static float get(const float& v, std::size_t) { return v; }
    static void set(float& v, std::size_t, float x) { v = x; }
};
template <>
struct Components<int> {
    static constexpr std::size_t count = 1;
    static constexpr ParamKind kind = ParamKind::Int;
    static float get(const int& v, std::size_t) { return static_cast<float>(v); }
    static void set(int& v, std::size_t, float x) { v = static_cast<int>(x >= 0 ? x + 0.5f : x - 0.5f); }
};
template <>
struct Components<bool> {
    static constexpr std::size_t count = 1;
    static constexpr ParamKind kind = ParamKind::Bool;
    static float get(const bool& v, std::size_t) { return v ? 1.0f : 0.0f; }
    static void set(bool& v, std::size_t, float x) { v = x >= 0.5f; }
};
template <glm::length_t N>
struct VecComponents {
    static constexpr std::size_t count = static_cast<std::size_t>(N);
    static float get(const glm::vec<N, float>& v, std::size_t i) { return v[static_cast<glm::length_t>(i)]; }
    static void set(glm::vec<N, float>& v, std::size_t i, float x) { v[static_cast<glm::length_t>(i)] = x; }
};
template <>
struct Components<glm::vec2> : VecComponents<2> {
    static constexpr ParamKind kind = ParamKind::Vec2;
};
template <>
struct Components<glm::vec3> : VecComponents<3> {
    static constexpr ParamKind kind = ParamKind::Vec3;
};
template <>
struct Components<glm::vec4> : VecComponents<4> {
    static constexpr ParamKind kind = ParamKind::Vec4;
};
} // namespace detail

template <typename T>
struct ParamDesc {
    std::string path;
    T defaultValue{};
    T hardMin{};
    T hardMax{};
    T softMin{};        // UI range; defaults to hard range when equal to hardMin/hardMax
    T softMax{};
    std::string label;  // defaults to the last path segment
    std::string group;  // defaults to the first path segment
    ParamFlags flags{};
    bool isColor = false; // for vec3/vec4: present as a colour picker
};

template <typename T>
class Parameter final : public IParameter {
public:
    using Comp = detail::Components<T>;

    explicit Parameter(ParamDesc<T> desc);

    [[nodiscard]] const std::string& path() const override { return desc_.path; }
    [[nodiscard]] const std::string& label() const override { return desc_.label; }
    [[nodiscard]] const std::string& group() const override { return desc_.group; }
    [[nodiscard]] ParamKind kind() const override { return desc_.isColor ? ParamKind::Color : Comp::kind; }
    [[nodiscard]] ParamFlags flags() const override { return desc_.flags; }
    [[nodiscard]] std::size_t componentCount() const override { return Comp::count; }

    [[nodiscard]] float baseComponent(std::size_t i) const override { return Comp::get(base_, i); }
    void setBaseComponent(std::size_t i, float v) override { Comp::set(base_, i, clampComponent(i, v)); }
    [[nodiscard]] float finalComponent(std::size_t i) const override { return Comp::get(final_, i); }
    void setFinalComponent(std::size_t i, float v) override { Comp::set(final_, i, clampComponent(i, v)); }
    [[nodiscard]] float defaultComponent(std::size_t i) const override { return Comp::get(desc_.defaultValue, i); }
    [[nodiscard]] float hardMin(std::size_t i) const override { return Comp::get(desc_.hardMin, i); }
    [[nodiscard]] float hardMax(std::size_t i) const override { return Comp::get(desc_.hardMax, i); }
    [[nodiscard]] float softMin(std::size_t i) const override { return Comp::get(desc_.softMin, i); }
    [[nodiscard]] float softMax(std::size_t i) const override { return Comp::get(desc_.softMax, i); }

    void resetToDefault() override {
        base_ = desc_.defaultValue;
        final_ = base_;
    }
    void resetFinal() override { final_ = base_; }

    // Typed access for scene code.
    [[nodiscard]] const T& value() const { return final_; } // modulated value for this frame
    [[nodiscard]] const T& base() const { return base_; }
    void setBase(const T& v) {
        for (std::size_t i = 0; i < Comp::count; ++i) {
            setBaseComponent(i, Comp::get(v, i));
        }
    }
    [[nodiscard]] const ParamDesc<T>& desc() const { return desc_; }

private:
    [[nodiscard]] float clampComponent(std::size_t i, float v) const {
        const float lo = hardMin(i);
        const float hi = hardMax(i);
        return v < lo ? lo : (v > hi ? hi : v);
    }

    ParamDesc<T> desc_;
    T base_{};
    T final_{};
};

template <typename T>
Parameter<T>::Parameter(ParamDesc<T> desc) : desc_(std::move(desc)) {
    if (desc_.label.empty()) {
        const auto slash = desc_.path.rfind('/');
        desc_.label = slash == std::string::npos ? desc_.path : desc_.path.substr(slash + 1);
    }
    if (desc_.group.empty()) {
        const auto slash = desc_.path.find('/');
        desc_.group = slash == std::string::npos ? std::string{} : desc_.path.substr(0, slash);
    }
    if (desc_.softMin == desc_.softMax) {
        desc_.softMin = desc_.hardMin;
        desc_.softMax = desc_.hardMax;
    }
    base_ = desc_.defaultValue;
    for (std::size_t i = 0; i < Comp::count; ++i) {
        Comp::set(base_, i, clampComponent(i, Comp::get(base_, i)));
    }
    final_ = base_;
}

} // namespace avgen::params
