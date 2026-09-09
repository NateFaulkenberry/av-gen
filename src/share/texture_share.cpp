#include "share/texture_share.hpp"

#include "share/share_backend.hpp"

#include <fmt/format.h>

namespace avgen::share {

TextureShare::TextureShare() = default;

TextureShare::~TextureShare() { close(); }

const char* TextureShare::kindName(ShareKind kind) {
    switch (kind) {
    case ShareKind::Syphon: return "Syphon";
    case ShareKind::Ndi: return "NDI";
    }
    return "unknown";
}

bool TextureShare::available(ShareKind kind) {
    switch (kind) {
    case ShareKind::Syphon: return syphonAvailable();
    case ShareKind::Ndi: return ndiAvailable();
    }
    return false;
}

std::string TextureShare::describe() { return fmt::format("Syphon: {}; NDI: {}", syphonDescribe(), ndiDescribe()); }

Result<void> TextureShare::open(ShareKind kind, gpu::Context& context, const std::string& name) {
    close();
    std::unique_ptr<ShareBackend> backend;
    switch (kind) {
    case ShareKind::Syphon: backend = createSyphonBackend(); break;
    case ShareKind::Ndi: backend = createNdiBackend(); break;
    }
    if (!backend) {
        return fail("{} sharing is not available on this system ({})", kindName(kind), describe());
    }
    backend->setFrameRate(frameRateN_, frameRateD_);
    if (auto opened = backend->open(context, name); !opened) {
        backend->close();
        return fail("{} \"{}\": {}", kindName(kind), name, opened.error().message);
    }
    backend_ = std::move(backend);
    kind_ = kind;
    name_ = name;
    return {};
}

void TextureShare::close() {
    if (backend_) {
        backend_->close();
        backend_.reset();
    }
}

Result<void> TextureShare::publish(const wgpu::Texture& source, std::uint32_t w, std::uint32_t h) {
    if (!backend_) {
        return fail("publish: share is not open");
    }
    if (!source || w == 0 || h == 0) {
        return fail("publish: empty source");
    }
    if (!isShareableFormat(source.GetFormat())) {
        return fail("publish: source must be RGBA8Unorm or BGRA8Unorm");
    }
    if (!(source.GetUsage() & wgpu::TextureUsage::CopySrc)) {
        return fail("publish: source texture lacks CopySrc usage");
    }
    if (source.GetWidth() < w || source.GetHeight() < h) {
        return fail("publish: region {}x{} exceeds the {}x{} source", w, h, source.GetWidth(), source.GetHeight());
    }
    return backend_->publish(source, w, h);
}

void TextureShare::setFrameRate(int numerator, int denominator) {
    if (numerator <= 0 || denominator <= 0) {
        return;
    }
    frameRateN_ = numerator;
    frameRateD_ = denominator;
    if (backend_) {
        backend_->setFrameRate(numerator, denominator);
    }
}

ShareStats TextureShare::stats() const {
    if (!backend_) {
        return {};
    }
    return backend_->stats();
}

} // namespace avgen::share
