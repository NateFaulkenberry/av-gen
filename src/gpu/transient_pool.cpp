#include "gpu/transient_pool.hpp"

#include "gpu/context.hpp"

#include <algorithm>

namespace avgen::gpu {

TransientTexture TransientPool::acquire(std::uint32_t width, std::uint32_t height, wgpu::TextureFormat format,
                                        wgpu::TextureUsage usage, const char* label) {
    for (auto& entry : entries_) {
        if (!entry.inUse && entry.texture.width == width && entry.texture.height == height &&
            entry.texture.format == format && entry.usage == usage) {
            entry.inUse = true;
            entry.lastUsedFrame = frame_;
            return entry.texture;
        }
    }
    wgpu::TextureDescriptor desc{};
    desc.label = label;
    desc.usage = usage;
    desc.dimension = wgpu::TextureDimension::e2D;
    desc.size = {std::max(1u, width), std::max(1u, height), 1};
    desc.format = format;
    Entry entry;
    entry.texture.texture = context_.device().CreateTexture(&desc);
    entry.texture.view = entry.texture.texture.CreateView();
    entry.texture.width = desc.size.width;
    entry.texture.height = desc.size.height;
    entry.texture.format = format;
    entry.usage = usage;
    entry.inUse = true;
    entry.lastUsedFrame = frame_;
    ++allocations_;
    entries_.push_back(entry);
    return entries_.back().texture;
}

void TransientPool::release(const TransientTexture& texture) {
    for (auto& entry : entries_) {
        if (entry.texture.texture.Get() == texture.texture.Get()) {
            entry.inUse = false;
            return;
        }
    }
}

void TransientPool::endFrame(std::uint32_t keepFrames) {
    ++frame_;
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const Entry& e) {
                                      return !e.inUse && frame_ - e.lastUsedFrame > keepFrames;
                                  }),
                   entries_.end());
    for (auto& entry : entries_) {
        entry.inUse = false; // anything still marked was leaked by a pass; reclaim
    }
}

std::size_t TransientPool::inUse() const {
    return static_cast<std::size_t>(std::count_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.inUse; }));
}

} // namespace avgen::gpu
