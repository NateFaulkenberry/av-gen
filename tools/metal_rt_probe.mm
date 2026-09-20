// Metal ray tracing: does it beat Embree on THIS machine? (ADR-383's open question, the audit's W6
// and section 5.)
//
// TEMPORARY. This exists to answer one question and should be deleted with the decision it
// informs, the way `avgen_charai_probe` and `avgen_seek_probe` say they should be.
//
// ## The question, and why it is a question
//
// The brief treats a Metal ray-tracing backend as obviously worth building. Measured on this
// machine it is not obvious:
//
//     device: Apple M2 Max     supportsRaytracing: 1
//     MTLGPUFamilyApple7: 1    Apple8: 1    Apple9: 0
//
// Apple's HARDWARE ray-tracing units arrive with M3 (`MTLGPUFamilyApple9`). On an M2 the API works
// and `intersect()` runs on the shader cores -- it is compute against a GPU-built BVH, not
// dedicated silicon. It may still beat a twelve-core Embree, because the GPU has far more ALUs. It
// may not, because the CPU path is already SIMD-wide, cache-friendly and shares the same unified
// memory. Building a backend to find out is weeks; this is a day.
//
// ## What it does, and what it deliberately does not
//
// It takes AV Gen's OWN geometry -- `pathtrace::buildSnapshot` on a real project, the same
// structure the Embree backend consumes -- builds a Metal primitive acceleration structure over
// it, casts the same camera rays `pathtrace::cameraBasis` generates, shades |N.L| into a
// scene-linear buffer and writes an EXR. Then it times the two halves separately, because "Metal
// is faster" is two different claims about acceleration-structure build and about tracing, and a
// scene rebuilt every frame cares about the first.
//
// It does NOT shade materials, sample lights, bounce, or handle instancing. Those are the backend;
// this is the decision about whether to write one. Comparing a full path trace against primary
// visibility would be comparing two different amounts of work.
//
// ## Reading the numbers
//
// Minima over repeats, never means (ADR-170), and it prints the machine's load so a contended run
// is visible as contended rather than quietly wrong. A number taken while another agent is
// rendering is not evidence, and this says so in its own output rather than leaving it to the
// reader.

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#import <simd/simd.h>

#include "app/engine.hpp"
#include "assets/exr.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "pathtrace/camera.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace avgen;

namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string loadAverage() {
    double load[3] = {0, 0, 0};
    if (getloadavg(load, 3) != 3) {
        return "unknown";
    }
    char buf[96];
    std::snprintf(buf, sizeof(buf), "%.2f %.2f %.2f", load[0], load[1], load[2]);
    return buf;
}

// The shading kernel. `intersect` against a primitive acceleration structure, then |N.L| with a
// fixed key -- enough to prove the rays hit the right triangles and to produce an image somebody
// can look at, and no more.
constexpr const char* kKernel = R"MSL(
#include <metal_stdlib>
#include <metal_raytracing>
using namespace metal;
using namespace raytracing;

struct Uniforms {
    float3 origin;
    float3 forward;
    float3 right;
    float3 up;
    float2 halfExtent;   // tan(fovY/2) * aspect, tan(fovY/2)
    uint   width;
    uint   height;
};

kernel void traceCamera(uint2 tid                                   [[thread_position_in_grid]],
                        constant Uniforms&               u          [[buffer(0)]],
                        device float4*                   out        [[buffer(1)]],
                        const device packed_float3*      positions  [[buffer(2)]],
                        const device uint*               indices    [[buffer(3)]],
                        primitive_acceleration_structure accel      [[buffer(4)]]) {
    if (tid.x >= u.width || tid.y >= u.height) { return; }
    const uint index = tid.y * u.width + tid.x;

    // The same ray `pathtrace::generateRay` makes: pixel centre, +X right, +Y UP in the image, and
    // the image's first row at the TOP -- which is why ndcY is negated. Getting this wrong flips
    // the picture and looks like a camera bug rather than a convention mismatch.
    const float ndcX = (2.0f * (float(tid.x) + 0.5f) / float(u.width)) - 1.0f;
    const float ndcY = 1.0f - (2.0f * (float(tid.y) + 0.5f) / float(u.height));
    const float3 dir = normalize(u.forward + u.right * (ndcX * u.halfExtent.x) +
                                 u.up * (ndcY * u.halfExtent.y));

    ray r;
    r.origin = u.origin;
    r.direction = dir;
    r.min_distance = 1e-4f;
    r.max_distance = 1e7f;

    intersector<triangle_data> isect;
    isect.assume_geometry_type(geometry_type::triangle);
    isect.force_opacity(forced_opacity::opaque);
    const intersection_result<triangle_data> hit = isect.intersect(r, accel);

    float3 colour = float3(0.0f);
    if (hit.type != intersection_type::none) {
        const uint tri = hit.primitive_id;
        const float3 a = float3(positions[indices[tri * 3 + 0]]);
        const float3 b = float3(positions[indices[tri * 3 + 1]]);
        const float3 c = float3(positions[indices[tri * 3 + 2]]);
        const float3 n = normalize(cross(b - a, c - a));
        const float3 key = normalize(float3(0.4f, 0.8f, 0.45f));
        colour = float3(abs(dot(n, key)));
    }
    out[index] = float4(colour, 1.0f);
}
)MSL";

struct Flattened {
    std::vector<simd_float3> positions;
    std::vector<std::uint32_t> indices;
};

// Every non-instanced mesh, concatenated. Instanced geometry is skipped and SAID to be skipped:
// an instance acceleration structure is real work and this probe is about whether that work is
// worth starting.
Flattened flatten(const pathtrace::Snapshot& snapshot, std::size_t& skippedInstances) {
    Flattened out;
    skippedInstances = snapshot.instanceCount();
    for (const pathtrace::TriangleMesh& mesh : snapshot.meshes) {
        const auto base = static_cast<std::uint32_t>(out.positions.size());
        for (const glm::vec3& p : mesh.positions) {
            out.positions.push_back(simd_make_float3(p.x, p.y, p.z));
        }
        for (const std::uint32_t i : mesh.indices) {
            out.indices.push_back(base + i);
        }
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    log::init(log::Level::Info);
    std::string project;
    double seconds = 0.0;
    std::uint32_t width = 1920, height = 1080;
    int repeats = 5;
    std::string outPath;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", name);
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--project") project = next("--project");
        else if (a == "--seconds") seconds = std::stod(next("--seconds"));
        else if (a == "--size") {
            const std::string v = next("--size");
            const auto x = v.find('x');
            width = static_cast<std::uint32_t>(std::stoul(v.substr(0, x)));
            height = static_cast<std::uint32_t>(std::stoul(v.substr(x + 1)));
        } else if (a == "--repeats") repeats = std::stoi(next("--repeats"));
        else if (a == "--out") outPath = next("--out");
        else {
            std::fprintf(stderr, "usage: avgen_metal_rt_probe --project <f.json> [--seconds t] "
                                 "[--size WxH] [--repeats n] [--out image.exr]\n");
            return 2;
        }
    }
    if (project.empty()) {
        std::fprintf(stderr, "--project is required\n");
        return 2;
    }

    @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil || ![device supportsRaytracing]) {
            std::fprintf(stderr, "no Metal device with ray tracing\n");
            return 3;
        }
        const bool apple9 = [device supportsFamily:MTLGPUFamilyApple9];
        std::printf("device: %s\n", [[device name] UTF8String]);
        std::printf("hardware ray tracing (MTLGPUFamilyApple9): %s\n", apple9 ? "YES" : "NO -- "
                    "intersect() runs on the shader cores");
        std::printf("machine load at start: %s\n", loadAverage().c_str());
        std::printf("  (a number taken while that is above ~2 is not evidence -- ADR-170)\n\n");

        // ---- AV Gen's own geometry, through AV Gen's own path --------------------------------
        app::Engine engine(app::EngineMode::Offline);
        if (auto ok = engine.loadProject(project); !ok) {
            std::fprintf(stderr, "project: %s\n", ok.error().message.c_str());
            return 4;
        }
        engine.update(FrameTime{seconds, 0.0, 0});
        engine.update(FrameTime{seconds, 1.0 / 60.0, 1});
        const pathtrace::Snapshot snapshot = pathtrace::buildSnapshot(engine.scene());
        std::size_t skippedInstances = 0;
        const Flattened geo = flatten(snapshot, skippedInstances);
        const std::size_t triangles = geo.indices.size() / 3;
        std::printf("snapshot: %zu stored triangles, %zu visible, %zu instances\n",
                    snapshot.triangleCount(), snapshot.visibleTriangleCount(),
                    snapshot.instanceCount());
        std::printf("probe geometry: %zu triangles, %zu vertices (instanced geometry SKIPPED: "
                    "%zu instances not represented)\n\n",
                    triangles, geo.positions.size(), skippedInstances);
        if (triangles == 0) {
            std::fprintf(stderr, "nothing to trace\n");
            return 5;
        }

        // ---- buffers --------------------------------------------------------------------------
        id<MTLBuffer> posBuf = [device newBufferWithBytes:geo.positions.data()
                                                   length:geo.positions.size() * sizeof(simd_float3)
                                                  options:MTLResourceStorageModeShared];
        id<MTLBuffer> idxBuf = [device newBufferWithBytes:geo.indices.data()
                                                   length:geo.indices.size() * sizeof(std::uint32_t)
                                                  options:MTLResourceStorageModeShared];

        MTLAccelerationStructureTriangleGeometryDescriptor* tri =
            [MTLAccelerationStructureTriangleGeometryDescriptor descriptor];
        tri.vertexBuffer = posBuf;
        tri.vertexStride = sizeof(simd_float3);
        tri.indexBuffer = idxBuf;
        tri.indexType = MTLIndexTypeUInt32;
        tri.triangleCount = triangles;
        tri.opaque = YES;

        MTLPrimitiveAccelerationStructureDescriptor* accelDesc =
            [MTLPrimitiveAccelerationStructureDescriptor descriptor];
        accelDesc.geometryDescriptors = @[tri];

        const MTLAccelerationStructureSizes sizes =
            [device accelerationStructureSizesWithDescriptor:accelDesc];
        std::printf("acceleration structure: %.1f MB, scratch %.1f MB\n",
                    double(sizes.accelerationStructureSize) / 1e6, double(sizes.buildScratchBufferSize) / 1e6);

        id<MTLCommandQueue> queue = [device newCommandQueue];

        // ---- AS build, timed, minima over repeats ------------------------------------------------
        double bestBuild = 1e9;
        id<MTLAccelerationStructure> accel = nil;
        for (int r = 0; r < repeats; ++r) {
            id<MTLAccelerationStructure> a =
                [device newAccelerationStructureWithSize:sizes.accelerationStructureSize];
            id<MTLBuffer> scratch = [device newBufferWithLength:std::max<NSUInteger>(sizes.buildScratchBufferSize, 1)
                                                        options:MTLResourceStorageModePrivate];
            const double t0 = nowSeconds();
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLAccelerationStructureCommandEncoder> enc = [cb accelerationStructureCommandEncoder];
            [enc buildAccelerationStructure:a descriptor:accelDesc scratchBuffer:scratch scratchBufferOffset:0];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            bestBuild = std::min(bestBuild, nowSeconds() - t0);
            accel = a;
        }
        std::printf("METAL acceleration structure build: %.1f ms (min of %d)\n\n", bestBuild * 1e3, repeats);

        // ---- the kernel --------------------------------------------------------------------------
        NSError* err = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:[NSString stringWithUTF8String:kKernel]
                                                  options:nil
                                                    error:&err];
        if (lib == nil) {
            std::fprintf(stderr, "kernel: %s\n", [[err localizedDescription] UTF8String]);
            return 6;
        }
        id<MTLComputePipelineState> pipeline =
            [device newComputePipelineStateWithFunction:[lib newFunctionWithName:@"traceCamera"] error:&err];
        if (pipeline == nil) {
            std::fprintf(stderr, "pipeline: %s\n", [[err localizedDescription] UTF8String]);
            return 6;
        }

        struct Uniforms {
            simd_float3 origin, forward, right, up;
            simd_float2 halfExtent;
            std::uint32_t width, height;
        };
        const pathtrace::CameraBasis basis = pathtrace::cameraBasis(snapshot.camera, width, height);
        Uniforms u{};
        u.origin = simd_make_float3(basis.origin.x, basis.origin.y, basis.origin.z);
        u.forward = simd_make_float3(basis.forward.x, basis.forward.y, basis.forward.z);
        u.right = simd_make_float3(basis.right.x, basis.right.y, basis.right.z);
        u.up = simd_make_float3(basis.up.x, basis.up.y, basis.up.z);
        // Exactly `generateRay`'s two factors: aspect * tanHalfFovY across, tanHalfFovY down.
        u.halfExtent = simd_make_float2(basis.aspect * basis.tanHalfFovY, basis.tanHalfFovY);
        u.width = width;
        u.height = height;

        id<MTLBuffer> uniBuf = [device newBufferWithBytes:&u length:sizeof(u)
                                                  options:MTLResourceStorageModeShared];
        id<MTLBuffer> outBuf = [device newBufferWithLength:sizeof(float) * 4 * width * height
                                                   options:MTLResourceStorageModeShared];

        double bestTrace = 1e9;
        for (int r = 0; r < repeats; ++r) {
            const double t0 = nowSeconds();
            id<MTLCommandBuffer> cb = [queue commandBuffer];
            id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
            [enc setComputePipelineState:pipeline];
            [enc setBuffer:uniBuf offset:0 atIndex:0];
            [enc setBuffer:outBuf offset:0 atIndex:1];
            [enc setBuffer:posBuf offset:0 atIndex:2];
            [enc setBuffer:idxBuf offset:0 atIndex:3];
            [enc setAccelerationStructure:accel atBufferIndex:4];
            [enc dispatchThreads:MTLSizeMake(width, height, 1)
                threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
            [enc endEncoding];
            [cb commit];
            [cb waitUntilCompleted];
            bestTrace = std::min(bestTrace, nowSeconds() - t0);
        }
        const double rays = double(width) * double(height);
        std::printf("METAL primary rays: %.1f ms (min of %d) -- %.1f Mrays/s at %ux%u\n",
                    bestTrace * 1e3, repeats, rays / bestTrace / 1e6, width, height);

        // ---- the control: it has to have HIT something -------------------------------------------
        const auto* px = static_cast<const float*>([outBuf contents]);
        std::size_t lit = 0;
        for (std::size_t i = 0; i < std::size_t(width) * height; ++i) {
            if (px[i * 4] > 0.0f) ++lit;
        }
        const double coverage = double(lit) / rays;
        std::printf("coverage: %.1f%% of pixels hit geometry\n", coverage * 100.0);
        if (lit == 0) {
            std::printf("\n*** THE PROBE MEASURED NOTHING. Every ray missed, so the timing above is "
                        "the cost of missing. ***\n");
            return 7;
        }

        if (!outPath.empty()) {
            std::vector<float> rgba(px, px + std::size_t(width) * height * 4);
            if (auto ok = assets::writeExr(outPath, width, height, rgba, /*half=*/false); !ok) {
                std::fprintf(stderr, "exr: %s\n", ok.error().message.c_str());
            } else {
                std::printf("wrote %s\n", outPath.c_str());
            }
        }

        std::printf("\nmachine load at end: %s\n", loadAverage().c_str());
        std::printf("\nTo compare, on the SAME snapshot and the same camera:\n");
        std::printf("  avgen --project %s --pathtrace /tmp/embree.exr --pt-seconds %.3f "
                    "--size %ux%u --pt-samples 1 --pt-depth 0\n",
                    project.c_str(), seconds, width, height);
        std::printf("  and read `buildSeconds` and `renderSeconds` off the pathtrace summary line.\n");
        std::printf("REMEMBER: Embree here traces the instanced geometry too, and this probe does "
                    "not. With %zu instances skipped that is not a like-for-like race.\n",
                    skippedInstances);
    }
    return 0;
}
