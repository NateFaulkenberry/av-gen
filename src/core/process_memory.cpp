#include "core/process_memory.hpp"

#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(__linux__)
#include <fstream>
#include <string>
#include <unistd.h>
#endif

namespace avgen::core {

ProcessMemory processMemory() {
    ProcessMemory m;
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        m.resident = info.resident_size;
        m.footprint = info.phys_footprint;
    }
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t size = 0;
    std::uint64_t pages = 0;
    if (statm >> size >> pages) {
        m.resident = pages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
        m.footprint = m.resident;
    }
#endif
    return m;
}

} // namespace avgen::core
