#include "comp/layer_stack.hpp"

#include "core/log.hpp"

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::comp {

LayerStack::LayerStack() = default;
LayerStack::~LayerStack() = default;

std::uint32_t LayerStack::nextId() {
    std::uint32_t highest = 0;
    for (const auto& layer : layers_) {
        highest = std::max(highest, layer->id);
    }
    return highest + 1;
}

Layer* LayerStack::find(std::uint32_t id) {
    const auto it = std::ranges::find_if(layers_, [id](const auto& l) { return l->id == id; });
    return it == layers_.end() ? nullptr : it->get();
}

const Layer* LayerStack::find(std::uint32_t id) const {
    const auto it = std::ranges::find_if(layers_, [id](const auto& l) { return l->id == id; });
    return it == layers_.end() ? nullptr : it->get();
}

Layer* LayerStack::at(std::size_t index) { return index < layers_.size() ? layers_[index].get() : nullptr; }

int LayerStack::indexOf(std::uint32_t id) const {
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (layers_[i]->id == id) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Layer& LayerStack::add(std::unique_ptr<Layer> layer) {
    if (layer->id == 0 || find(layer->id) != nullptr) {
        layer->id = nextId();
    }
    layers_.push_back(std::move(layer));
    geometryDirty_ = true;
    return *layers_.back();
}

TextLayer& LayerStack::addText(std::string text, double startSeconds, double endSeconds) {
    auto layer = std::make_unique<TextLayer>();
    layer->setText(std::move(text));
    layer->startTime = startSeconds;
    layer->endTime = endSeconds;
    auto& ref = static_cast<TextLayer&>(add(std::move(layer)));
    ref.name = fmt::format("Text {}", ref.id);
    return ref;
}

ShapeLayer& LayerStack::addShape(ShapeKind shape) {
    auto layer = std::make_unique<ShapeLayer>();
    layer->shape = shape;
    if (shape == ShapeKind::Ellipse) {
        layer->size = glm::vec2(0.2f, 0.2f);
    } else if (shape == ShapeKind::Line) {
        layer->size = glm::vec2(0.6f, 0.004f);
    }
    auto& ref = static_cast<ShapeLayer&>(add(std::move(layer)));
    ref.name = fmt::format("{} {}", shapeKindName(shape), ref.id);
    return ref;
}

bool LayerStack::remove(std::uint32_t id) {
    const auto it = std::ranges::find_if(layers_, [id](const auto& l) { return l->id == id; });
    if (it == layers_.end()) {
        return false;
    }
    (*it)->detach();
    layers_.erase(it);
    geometryDirty_ = true;
    return true;
}

Layer* LayerStack::duplicate(std::uint32_t id) {
    const Layer* source = find(id);
    if (source == nullptr) {
        return nullptr;
    }
    auto copy = source->clone();
    copy->id = 0;
    copy->name = source->name + " copy";
    copy->detach();
    const int index = indexOf(id);
    Layer& added = add(std::move(copy));
    if (index >= 0) {
        // Directly above the original, which is where a duplicate belongs in a stack.
        moveTo(added.id, static_cast<std::size_t>(index) + 1);
    }
    return find(added.id);
}

bool LayerStack::moveTo(std::uint32_t id, std::size_t index) {
    const int from = indexOf(id);
    if (from < 0) {
        return false;
    }
    index = std::min(index, layers_.size() - 1);
    if (static_cast<std::size_t>(from) == index) {
        return true;
    }
    auto layer = std::move(layers_[static_cast<std::size_t>(from)]);
    layers_.erase(layers_.begin() + from);
    layers_.insert(layers_.begin() + static_cast<std::ptrdiff_t>(index), std::move(layer));
    geometryDirty_ = true;
    return true;
}

void LayerStack::clear() {
    for (auto& layer : layers_) {
        layer->detach();
    }
    layers_.clear();
    slots_.clear();
    vertices_.clear();
    runs_.clear();
    frame_ = {};
    itemCount_ = 0;
    geometryDirty_ = true;
    attached_ = false;
    ++vertexVersion_;
}

void LayerStack::attach(params::ParameterSet& set) {
    for (auto& layer : layers_) {
        layer->attach(set);
    }
    attached_ = true;
}

void LayerStack::detach() {
    for (auto& layer : layers_) {
        layer->detach();
    }
    attached_ = false;
}

void LayerStack::pullAuthored() {
    for (auto& layer : layers_) {
        layer->pullAuthored();
    }
}

void LayerStack::pushAuthored() {
    for (auto& layer : layers_) {
        layer->pushAuthored();
    }
}

std::vector<std::string> LayerStack::parameterPaths() const {
    std::vector<std::string> out;
    for (const auto& layer : layers_) {
        for (auto& path : layer->parameterPaths()) {
            out.push_back(std::move(path));
        }
    }
    return out;
}

void LayerStack::removeParameters(params::ParameterSet& set, const Layer& layer) const {
    for (const std::string& path : layer.parameterPaths()) {
        set.remove(path);
    }
}

bool LayerStack::geometryStale() const {
    if (geometryDirty_ || slots_.size() != layers_.size()) {
        return true;
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        if (slots_[i].geometryVersion != layers_[i]->geometryVersion()) {
            return true;
        }
    }
    return false;
}

void LayerStack::rebuildGeometry() {
    scratch_.clear();
    runs_.clear();
    slots_.assign(layers_.size(), Slot{});
    itemCount_ = 0;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        Layer& layer = *layers_[i];
        Slot& slot = slots_[i];
        slot.firstRun = static_cast<std::uint32_t>(runs_.size());
        slot.firstItem = itemCount_;
        slot.itemCount = layer.buildGeometry(scratch_, runs_, itemCount_, atlas_);
        slot.runCount = static_cast<std::uint32_t>(runs_.size()) - slot.firstRun;
        slot.geometryVersion = layer.geometryVersion();
        itemCount_ += slot.itemCount;
    }
    // Repack: primary runs in stack order first, then the secondary ones. A run that is skipped
    // this frame then only ever sits at the end of the buffer, so the runs that *are* drawn stay
    // contiguous and merge into one draw. Vertices carry their item index, so moving them is a
    // straight copy.
    vertices_.clear();
    vertices_.reserve(scratch_.size());
    for (int pass = 0; pass < 2; ++pass) {
        for (LayerRun& run : runs_) {
            if (run.secondary != (pass == 1)) {
                continue;
            }
            const std::uint32_t moved = static_cast<std::uint32_t>(vertices_.size());
            vertices_.insert(vertices_.end(), scratch_.begin() + run.firstVertex,
                             scratch_.begin() + run.firstVertex + run.vertexCount);
            run.firstVertex = moved;
        }
    }
    scratch_.clear();
    scratch_.shrink_to_fit();
    geometryDirty_ = false;
    atlasVersion_ = atlas_.version();
    ++vertexVersion_;
}

const CompositionFrame& LayerStack::build(const Frame& frame, double seconds) {
    if (geometryStale()) {
        rebuildGeometry();
    }
    frame_.items.assign(itemCount_, LayerItem{});
    frame_.draws.clear();
    frame_.drawnLayers = 0;
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const Slot& slot = slots_[i];
        if (slot.itemCount == 0) {
            continue;
        }
        layers_[i]->buildItems(frame, seconds, frame_.items.data() + slot.firstItem);
    }
    for (std::size_t i = 0; i < layers_.size(); ++i) {
        const Layer& layer = *layers_[i];
        const Slot& slot = slots_[i];
        if (!layer.enabled || slot.runCount == 0 || !layer.liveAt(seconds)) {
            continue;
        }
        bool drew = false;
        for (std::uint32_t r = 0; r < slot.runCount; ++r) {
            const LayerRun& run = runs_[slot.firstRun + r];
            const LayerItem& item = frame_.items[run.item];
            // Nothing this item can contribute is visible: the drop shadow that is switched off,
            // the layer keyframed to zero opacity. Dropped here rather than in the shader, so a
            // composition of a hundred layers with two on screen costs two draws.
            if (item.color.a <= 0.0f && item.accent.a <= 0.0f && item.glow.a <= 0.0f) {
                continue;
            }
            if (!frame_.draws.empty()) {
                CompositionDraw& last = frame_.draws.back();
                if (last.blend == layer.blend && last.firstVertex + last.vertexCount == run.firstVertex) {
                    last.vertexCount += run.vertexCount;
                    drew = true;
                    continue;
                }
            }
            frame_.draws.push_back({run.firstVertex, run.vertexCount, layer.blend});
            drew = true;
        }
        if (drew) {
            ++frame_.drawnLayers;
        }
    }
    return frame_;
}

nlohmann::json LayerStack::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    j["version"] = kFormatVersion;
    j["reference"] = {{"width", reference.width}, {"height", reference.height}};
    nlohmann::json array = nlohmann::json::array();
    for (const auto& layer : layers_) {
        array.push_back(layer->toJson());
    }
    j["layers"] = std::move(array);
    return j;
}

Result<void> LayerStack::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("composition must be an object");
    }
    const int version = j.value("version", kFormatVersion);
    if (version > kFormatVersion) {
        return fail("composition version {} is newer than supported version {}", version, kFormatVersion);
    }
    std::vector<std::unique_ptr<Layer>> loaded;
    if (const auto it = j.find("layers"); it != j.end()) {
        if (!it->is_array()) {
            return fail("composition 'layers' must be an array");
        }
        for (const auto& entry : *it) {
            auto layer = Layer::fromJson(entry);
            if (!layer) {
                return std::unexpected(layer.error());
            }
            loaded.push_back(std::move(*layer));
        }
    }
    // Nothing is mutated until the whole document parses.
    clear();
    if (const auto it = j.find("reference"); it != j.end() && it->is_object()) {
        reference.width = it->value("width", 1920u);
        reference.height = it->value("height", 1080u);
    } else {
        reference = Frame{1920, 1080};
    }
    for (auto& layer : loaded) {
        add(std::move(layer));
    }
    return {};
}

} // namespace avgen::comp
