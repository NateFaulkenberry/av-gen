#include "gpu/context.hpp"

#include "core/log.hpp"
#include "gpu/surface.hpp"

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
    primarySurface_.reset(); // unconfigures before the device goes away
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

    // ---- primary surface (optional); created before the adapter so it can be compatible ----
    wgpu::Surface rawSurface;
    if (desc.metalLayer != nullptr) {
        wgpu::SurfaceSourceMetalLayer metalSource{};
        metalSource.layer = desc.metalLayer;
        wgpu::SurfaceDescriptor surfaceDesc{};
        surfaceDesc.nextInChain = &metalSource;
        surfaceDesc.label = "avgen-surface";
        rawSurface = ctx->instance_.CreateSurface(&surfaceDesc);
        if (!rawSurface) {
            return fail("failed to create a WebGPU surface from the Metal layer");
        }
    }

    // ---- adapter ----
    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.powerPreference =
        desc.preferHighPerformance ? wgpu::PowerPreference::HighPerformance : wgpu::PowerPreference::LowPower;
    adapterOptions.compatibleSurface = rawSurface;
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

    // ---- primary surface format ----
    if (rawSurface) {
        ctx->primarySurface_ = std::unique_ptr<Surface>(new Surface(*ctx, std::move(rawSurface)));
        if (auto r = ctx->primarySurface_->chooseFormat(); !r) {
            return std::unexpected(r.error());
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

wgpu::TextureFormat Context::surfaceFormat() const {
    return primarySurface_ ? primarySurface_->format() : wgpu::TextureFormat::Undefined;
}

std::uint32_t Context::surfaceWidth() const { return primarySurface_ ? primarySurface_->width() : 0; }

std::uint32_t Context::surfaceHeight() const { return primarySurface_ ? primarySurface_->height() : 0; }

void Context::configureSurface(std::uint32_t width, std::uint32_t height) {
    if (primarySurface_) {
        primarySurface_->configure(width, height);
    }
}

Result<wgpu::TextureView> Context::acquireSurfaceView() {
    if (!primarySurface_) {
        return fail("no configured surface");
    }
    return primarySurface_->acquire();
}

void Context::present() {
    if (primarySurface_) {
        primarySurface_->present();
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
