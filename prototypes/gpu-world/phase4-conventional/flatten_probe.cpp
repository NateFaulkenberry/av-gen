// GPU World spike, Phase 4 -- DISPOSABLE. The conventional meadow through production's real flatten.
//
// Times Composition::loadFile, then attach() (which performs the first, cold flatten: terrain build,
// ecology scatter, records), then a forced re-flatten with nothing changed. Reports the flattened
// scene's procedural objects and instance counts, the record bytes, and the process footprint at each
// step. CPU only: no GPU, no renderer.
#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"

#include <mach/mach.h>

#include <chrono>
#include <cstdio>
#include <filesystem>

using namespace avgen;
namespace {
double msSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}
double footprintMb() {
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) return -1;
    return info.phys_footprint / 1048576.0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "flatten_probe <abs scene path>\n"); return 2; }
    const std::filesystem::path path = argv[1];
    log::setLevel(log::Level::Warn);
    const double fp0 = footprintMb();
    assets::AssetRegistry registry{path.parent_path()};
    auto t0 = std::chrono::steady_clock::now();
    auto loaded = scene::Composition::loadFile(path, registry);
    const double loadMs = msSince(t0);
    if (!loaded) { std::fprintf(stderr, "load: %s\n", loaded.error().message.c_str()); return 1; }
    scene::Composition& comp = **loaded;
    params::ParameterSet params;
    params::Modulator modulator;
    const double fp1 = footprintMb();
    const auto before = comp.flattenCount();
    t0 = std::chrono::steady_clock::now();
    comp.attach(params, modulator);
    (void)comp.nodeCorners(comp.nodes().front()->name);
    const double coldMs = msSince(t0);
    const auto afterCold = comp.flattenCount();
    const double fp2 = footprintMb();
    comp.setComposition(comp.composition());
    t0 = std::chrono::steady_clock::now();
    (void)comp.nodeCorners(comp.nodes().front()->name);
    const double warmMs = msSince(t0);
    const auto afterWarm = comp.flattenCount();
    const scene::Scene& s = comp.scene();
    std::uint64_t instances = 0, recordBytes = 0;
    for (const auto& p : s.procedurals) { instances += p.instances.size(); recordBytes += p.instances.size() * sizeof(p.instances[0]); }
    std::printf("{\"scene\":\"%s\",\"load_ms\":%.1f,\"cold_flatten_ms\":%.1f,\"cold_flattens\":%llu,\"warm_flatten_ms\":%.1f,"
                "\"warm_flattens\":%llu,\"procedural_objects\":%zu,\"instances\":%llu,\"record_bytes\":%llu,\"record_size\":%zu,"
                "\"entities\":%zu,\"meshes\":%zu,\"footprint_start_mb\":%.1f,\"footprint_loaded_mb\":%.1f,\"footprint_flattened_mb\":%.1f,"
                "\"footprint_end_mb\":%.1f,\"scene_file_bytes\":%llu}\n",
                path.filename().string().c_str(), loadMs, coldMs, static_cast<unsigned long long>(afterCold - before), warmMs,
                static_cast<unsigned long long>(afterWarm - afterCold), s.procedurals.size(), static_cast<unsigned long long>(instances),
                static_cast<unsigned long long>(recordBytes), sizeof(s.procedurals.empty() ? spatial::InstanceRecord{} : s.procedurals[0].instances[0]),
                s.entities.size(), s.meshes.size(), fp0, fp1, fp2, footprintMb(),
                static_cast<unsigned long long>(std::filesystem::file_size(path)));
    return 0;
}
