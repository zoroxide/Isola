#pragma once
#include <GL/glew.h>
#include <string>
#include <vector>

// What GPU are we running on? Used to pick a starting quality tier before the benchmark,
// and written to gpu_report.txt so performance problems on other machines can be diagnosed.
struct GpuInfo {
    enum Vendor { Intel, AMD, Nvidia, Software, Other };
    Vendor vendor = Other;
    std::string vendorStr, renderer, version, glsl;
    int vramMB = -1;            // dedicated video memory when the driver reports it
    bool legacy = false;        // pre-GCN AMD / old Intel / old NVIDIA: very limited GPUs
    bool s3tc = false, anisotropic = false;
    int suggestedTier = 2;      // starting tier before / without the benchmark
    std::string vendorName() const;
};

struct BenchmarkSample { int tier; float scale; float ms; };

namespace GpuProfile {
GpuInfo detect();
// graphics.cfg: the tier chosen by the benchmark for this exact GPU + driver
bool loadCache(const std::string& path, const GpuInfo& gpu, int& tier);
void saveCache(const std::string& path, const GpuInfo& gpu, int tier);
// gpu_report.txt: everything needed to diagnose performance on another machine
void writeReport(const std::string& path, const GpuInfo& gpu, int chosenTier, bool fromCache,
                 const std::vector<BenchmarkSample>& samples, const std::string& passTimes,
                 const std::string& shaderLog);
}
