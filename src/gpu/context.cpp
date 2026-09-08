#include "gpu/context.hpp"

#include "core/log.hpp"

#include <array>
#include <cstring>
#include <vector>

namespace avgen::gpu {

namespace {

const char* backendName(wgpu::BackendType type) {
    switch (type) {
    case wgpu::BackendType::Metal: return "Metal";
    case wgpu::BackendType::Vulkan: return "Vulkan";
    case wgpu::BackendType::D3D12: return "D3D12";
    case wgpu::BackendType::D3D11: return "D3D11";
    case wgpu::BackendType::OpenGL: return "OpenGL";
    case wgpu::BackendType::OpenGLES: return "OpenGLES";
    case wgpu::BackendType::WebGPU: return "WebGPU";
    case wgpu::BackendType::Null: return "Null";
    default: return "Unknown";
    }
}

const char* adapterTypeName(wgpu::AdapterType type) {
    switch (type) {
    case wgpu::AdapterType::DiscreteGPU: return "discrete";
    case wgpu::AdapterType::IntegratedGPU: return "integrated";
    case wgpu::AdapterType::CPU: return "cpu";
    default: return "unknown";
    }
}

const char* errorTypeName(wgpu::ErrorType type) {
    switch (type) {
    case wgpu::ErrorType::Validation: return "validation";
    case wgpu::ErrorType::OutOfMemory: return "out-of-memory";
    case wgpu::ErrorType::Internal: return "internal";
    case wgpu::ErrorType::Unknown: return "unknown";
    default: return "error";
    }
}

} // namespace

std::string Context::toString(wgpu::StringView view) {
    if (view.data == nullptr) {
        return {};
    }
    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }
    return std::string(view.data, view.length);
}

Context::~Context() {
    if (surface_ && surfaceConfigured_) {
        surface_.Unconfigure();
    }
}

Result<std::unique_ptr<Context>> Context::create(const ContextDesc& desc) {
    std::unique_ptr<Context> ctx(new Context());

    // ---- instance ----
    const std::array<wgpu::InstanceFeatureName, 1> instanceFeatures = {wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor instanceDesc{};
    instanceDesc.requiredFeatureCount = instanceFeatures.size();
    instanceDesc.requiredFeatures = instanceFeatures.data();
    ctx->instance_ = wgpu::CreateInstance(&instanceDesc);
    if (!ctx->instance_) {
        return fail("wgpuCreateInstance failed (TimedWaitAny unsupported?)");
    }

    // ---- surface (optional) ----
    if (desc.metalLayer != nullptr) {
        wgpu::SurfaceSourceMetalLayer metalSource{};
        metalSource.layer = desc.metalLayer;
        wgpu::SurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &metalSource;
        surfaceDesc.label = "avgen-surface";
        ctx->surface_ = ctx->instance_.CreateSurface(&surfaceDesc);
        if (!ctx->surface_) {
            return fail("failed to create a WebGPU surface from the Metal layer");
        }
    }

    // ---- adapter ----
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference =
        desc.preferHighPerformance ? wgpu::PowerPreference::HighPerformance : wgpu::PowerPreference::LowPower;
    adapterOptions.compatibleSurface = ctx->surface_;
#if defined(__APPLE__)
    adapterOptions.backendType = wgpu::BackendType::Metal;
#endif
    std::string adapterError;
    auto adapterFuture = ctx->instance_.RequestAdapter(
        &adapterOptions, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status == wgpu::RequestAdapterStatus::Success) {
                ctx->adapter_ = std::move(adapter);
            } else {
                adapterError = toString(message);
            }
        });
    if (ctx->instance_.WaitAny(adapterFuture, 5'000'000'000ull) != wgpu::WaitStatus::Success || !ctx->adapter_) {
        return fail("no suitable GPU adapter: {}", adapterError.empty() ? "timeout" : adapterError);
    }

    wgpu::AdapterInfo info{};
    ctx->adapter_.GetInfo(&info);
    ctx->caps_.adapterName = toString(info.device);
    ctx->caps_.vendor = toString(info.vendor);
    ctx->caps_.backendName = backendName(info.backendType);
    ctx->caps_.adapterType = adapterTypeName(info.adapterType);
    ctx->caps_.timestampQuery = desc.requestTimestamps && ctx->adapter_.HasFeature(wgpu::FeatureName::TimestampQuery);

    // Request the adapter's full limits so nothing is capped at WebGPU defaults (ADR-001).
    wgpu::Limits adapterLimits{};
    ctx->adapter_.GetLimits(&adapterLimits);
    adapterLimits.nextInChain = nullptr;

    // ---- device ----
    std::vector<wgpu::FeatureName> features;
    if (ctx->caps_.timestampQuery) {
        features.push_back(wgpu::FeatureName::TimestampQuery);
    }
    wgpu::DeviceDescriptor deviceDesc{};
    deviceDesc.label = desc.label.c_str();
    deviceDesc.requiredFeatureCount = features.size();
    deviceDesc.requiredFeatures = features.data();
    deviceDesc.requiredLimits = &adapterLimits;
    // Repeatable callbacks must be capture-less; the context pointer travels as userdata. The
    // Context lives on the heap behind the unique_ptr, so its address is stable.
    Context* raw = ctx.get();
    deviceDesc.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message, Context* self) {
            self->onError(type, message);
        },
        raw);
    deviceDesc.SetDeviceLostCallback(
        wgpu::CallbackMode::AllowSpontaneous,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message, Context* self) {
            if (reason != wgpu::DeviceLostReason::Destroyed) {
                self->deviceLost_ = true;
                log::error("GPU device lost: {}", toString(message));
            }
        },
        raw);

    std::string deviceError;
    auto deviceFuture = ctx->adapter_.RequestDevice(
        &deviceDesc, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
            if (status == wgpu::RequestDeviceStatus::Success) {
                ctx->device_ = std::move(device);
            } else {
                deviceError = toString(message);
            }
        });
    if (ctx->instance_.WaitAny(deviceFuture, 5'000'000'000ull) != wgpu::WaitStatus::Success || !ctx->device_) {
        return fail("failed to create GPU device: {}", deviceError.empty() ? "timeout" : deviceError);
    }
    ctx->queue_ = ctx->device_.GetQueue();
    ctx->device_.GetLimits(&ctx->caps_.limits);
    ctx->caps_.limits.nextInChain = nullptr;

    ctx->device_.SetLoggingCallback([](wgpu::LoggingType type, wgpu::StringView message) {
        const auto text = toString(message);
        switch (type) {
        case wgpu::LoggingType::Error: log::error("[wgpu] {}", text); break;
        case wgpu::LoggingType::Warning: log::warn("[wgpu] {}", text); break;
        default: log::debug("[wgpu] {}", text); break;
        }
    });

    // ---- surface format ----
    if (ctx->surface_) {
        wgpu::SurfaceCapabilities surfaceCaps{};
        if (ctx->surface_.GetCapabilities(ctx->adapter_, &surfaceCaps) != wgpu::Status::Success ||
            surfaceCaps.formatCount == 0) {
            return fail("surface reports no supported formats");
        }
        ctx->surfaceFormat_ = surfaceCaps.formats[0];
        for (std::size_t i = 0; i < surfaceCaps.formatCount; ++i) {
            if (surfaceCaps.formats[i] == wgpu::TextureFormat::BGRA8Unorm) {
                ctx->surfaceFormat_ = wgpu::TextureFormat::BGRA8Unorm;
                break;
            }
        }
    }

    log::info("GPU: {} ({}, {}) via {}; timestamps={}; maxColorAttachments={}, maxBufferSize={} MB",
              ctx->caps_.adapterName, ctx->caps_.vendor, ctx->caps_.adapterType, ctx->caps_.backendName,
              ctx->caps_.timestampQuery, ctx->caps_.limits.maxColorAttachments,
              ctx->caps_.limits.maxBufferSize / (1024 * 1024));
    return ctx;
}

void Context::onError(wgpu::ErrorType type, wgpu::StringView message) {
    ++errorCount_;
    lastError_ = toString(message);
    log::error("[wgpu {}] {}", errorTypeName(type), lastError_);
}

void Context::configureSurface(std::uint32_t width, std::uint32_t height) {
    if (!surface_ || width == 0 || height == 0) {
        return;
    }
    if (surfaceConfigured_ && width == surfaceWidth_ && height == surfaceHeight_) {
        return;
    }
    wgpu::SurfaceConfiguration config{};
    config.device = device_;
    config.format = surfaceFormat_;
    config.usage = wgpu::TextureUsage::RenderAttachment;
    config.width = width;
    config.height = height;
    config.alphaMode = wgpu::CompositeAlphaMode::Auto;
    config.presentMode = wgpu::PresentMode::Fifo;
    surface_.Configure(&config);
    surfaceWidth_ = width;
    surfaceHeight_ = height;
    surfaceConfigured_ = true;
    log::debug("surface configured {}x{}", width, height);
}

Result<wgpu::TextureView> Context::acquireSurfaceView() {
    if (!surface_ || !surfaceConfigured_) {
        return fail("no configured surface");
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        wgpu::SurfaceTexture surfaceTexture{};
        surface_.GetCurrentTexture(&surfaceTexture);
        switch (surfaceTexture.status) {
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal:
        case wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal: {
            wgpu::TextureViewDescriptor viewDesc{};
            viewDesc.label = "swapchain-view";
            viewDesc.format = surfaceFormat_;
            viewDesc.dimension = wgpu::TextureViewDimension::e2D;
            return surfaceTexture.texture.CreateView(&viewDesc);
        }
        case wgpu::SurfaceGetCurrentTextureStatus::Timeout:
        case wgpu::SurfaceGetCurrentTextureStatus::Outdated:
        case wgpu::SurfaceGetCurrentTextureStatus::Lost: {
            // Reconfigure once and retry.
            surfaceConfigured_ = false;
            const auto w = surfaceWidth_;
            const auto h = surfaceHeight_;
            surfaceWidth_ = 0;
            configureSurface(w, h);
            break;
        }
        case wgpu::SurfaceGetCurrentTextureStatus::Error:
        default:
            return fail("surface texture acquisition error");
        }
    }
    return fail("surface texture unavailable after reconfigure");
}

void Context::present() {
    if (surface_ && surfaceConfigured_) {
        surface_.Present();
    }
}

void Context::processEvents() { instance_.ProcessEvents(); }

bool Context::waitFor(wgpu::Future future, std::uint64_t timeoutNs) {
    return instance_.WaitAny(future, timeoutNs) == wgpu::WaitStatus::Success;
}

void Context::waitForQueue() {
    auto future = queue_.OnSubmittedWorkDone(wgpu::CallbackMode::WaitAnyOnly,
                                             [](wgpu::QueueWorkDoneStatus, wgpu::StringView) {});
    waitFor(future);
}

} // namespace avgen::gpu
