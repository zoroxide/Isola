#include "GpuProfile.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <sstream>

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}
bool has(const std::string& s, const char* what) { return s.find(what) != std::string::npos; }
const char* str(GLenum e) { const GLubyte* p = glGetString(e); return p ? (const char*)p : ""; }

// Number following a prefix, e.g. "hd 8490" -> 8490 for prefix "hd "
int numberAfter(const std::string& s, const std::string& prefix) {
    size_t p = s.find(prefix);
    if (p == std::string::npos) return -1;
    p += prefix.size();
    int n = 0, digits = 0;
    while (p < s.size() && std::isdigit((unsigned char)s[p])) { n = n * 10 + (s[p] - '0'); ++p; ++digits; }
    return digits ? n : -1;
}
} // namespace

std::string GpuInfo::vendorName() const {
    switch (vendor) {
    case Intel: return "Intel";
    case AMD: return "AMD";
    case Nvidia: return "NVIDIA";
    case Software: return "Software renderer";
    default: return "Unknown";
    }
}

GpuInfo GpuProfile::detect() {
    GpuInfo g;
    g.vendorStr = str(GL_VENDOR);
    g.renderer = str(GL_RENDERER);
    g.version = str(GL_VERSION);
    g.glsl = str(GL_SHADING_LANGUAGE_VERSION);
    std::string v = lower(g.vendorStr), r = lower(g.renderer);
    if (has(r, "llvmpipe") || has(r, "softpipe") || has(r, "swrast") || has(r, "microsoft basic")) g.vendor = GpuInfo::Software;
    else if (has(v, "intel") || has(r, "intel")) g.vendor = GpuInfo::Intel;
    else if (has(v, "ati") || has(v, "amd") || has(r, "radeon") || has(r, "amd")) g.vendor = GpuInfo::AMD;
    else if (has(v, "nvidia") || has(r, "geforce") || has(r, "quadro")) g.vendor = GpuInfo::Nvidia;

    g.s3tc = GLEW_EXT_texture_compression_s3tc;
    g.anisotropic = GLEW_EXT_texture_filter_anisotropic;
    // Video memory: AMD (GL_ATI_meminfo, free memory in KB) or NVIDIA (GL_NVX_gpu_memory_info)
    if (GLEW_ATI_meminfo) {
        GLint mem[4] = { 0, 0, 0, 0 };
        glGetIntegerv(0x87FC /*GL_TEXTURE_FREE_MEMORY_ATI*/, mem);
        if (mem[0] > 0) g.vramMB = mem[0] / 1024;
    } else if (GLEW_NVX_gpu_memory_info) {
        GLint kb = 0;
        glGetIntegerv(0x9048 /*GL_GPU_MEMORY_TOTAL_AVAILABLE_MEMORY_NVX*/, &kb);
        if (kb > 0) g.vramMB = kb / 1024;
    }
    while (glGetError() != GL_NO_ERROR) {}

    // Starting tier from the GPU family (the benchmark refines it)
    switch (g.vendor) {
    case GpuInfo::Software:
        g.legacy = true; g.suggestedTier = 0;
        break;
    case GpuInfo::AMD: {
        int hd = numberAfter(r, "hd ");
        bool modern = has(r, "rx ") || has(r, "navi") || has(r, "radeon pro") || has(r, "r9 ") || has(r, "780m") || has(r, "680m");
        bool apu = has(r, "radeon(tm) graphics") || has(r, "radeon graphics") || has(r, "vega 8") || has(r, "vega 6") ||
                   has(r, "vega 3") || has(r, "r7 graphics");
        if (hd >= 5000 && hd < 9000) {
            // Radeon HD 5000-8000: TeraScale (VLIW) or entry GCN, often 64-bit DDR3: very little bandwidth
            g.legacy = true;
            int model = hd % 1000;
            g.suggestedTier = (model < 500 || (hd < 7000 && model < 700)) ? 0 : 1;
        } else if (apu) {
            g.suggestedTier = 2;
        } else if (has(r, "r5 ") || has(r, "r7 ")) {
            g.suggestedTier = 1;
        } else if (modern) {
            g.suggestedTier = 3;
        } else {
            g.suggestedTier = 1;
        }
        break;
    }
    case GpuInfo::Intel:
        if (has(r, "iris") || has(r, " xe") || has(r, "arc")) g.suggestedTier = 3;
        else if (has(r, "hd graphics 2") || has(r, "hd graphics 3") || has(r, "hd graphics 4") || has(r, "hd graphics (") ||
                 has(r, "gma")) { g.legacy = true; g.suggestedTier = 0; }
        else g.suggestedTier = 2;   // HD 5xx / 6xx / UHD
        break;
    case GpuInfo::Nvidia:
        if (has(r, "rtx") || has(r, "gtx")) g.suggestedTier = 3;
        else if (has(r, "gt ") || has(r, "mx")) g.suggestedTier = 1;
        else g.suggestedTier = 2;
        break;
    default:
        g.suggestedTier = 1;
    }
    // Little video memory: keep things small
    if (g.vramMB > 0 && g.vramMB <= 1100) g.suggestedTier = std::min(g.suggestedTier, 1);
    return g;
}

bool GpuProfile::loadCache(const std::string& path, const GpuInfo& gpu, int& tier) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line, renderer, version;
    int t = -1, fileVersion = 0;
    while (std::getline(in, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        if (key == "renderer") renderer = val;
        else if (key == "driver") version = val;
        else if (key == "tier") t = std::atoi(val.c_str());
        else if (key == "format") fileVersion = std::atoi(val.c_str());
    }
    // Only valid for the same GPU and driver (a driver update can change performance a lot)
    if (fileVersion != 2 || renderer != gpu.renderer || version != gpu.version || t < 0 || t > 3) return false;
    tier = t;
    return true;
}

void GpuProfile::saveCache(const std::string& path, const GpuInfo& gpu, int tier) {
    std::ofstream out(path);
    out << "# Graphics quality chosen by the GPU benchmark. Delete this file to benchmark again.\n";
    out << "format=2\nrenderer=" << gpu.renderer << "\ndriver=" << gpu.version << "\ntier=" << tier << "\n";
}

void GpuProfile::writeReport(const std::string& path, const GpuInfo& gpu, int chosenTier, bool fromCache,
                             const std::vector<BenchmarkSample>& samples, const std::string& passTimes,
                             const std::string& shaderLog) {
    static const char* names[4] = { "Potato", "Low", "Medium", "High" };
    std::ofstream out(path);
    std::time_t now = std::time(nullptr);
    out << "Nut GPU report - " << std::ctime(&now) << "\n";
    out << "Vendor:    " << gpu.vendorStr << " (" << gpu.vendorName() << ")\n";
    out << "Renderer:  " << gpu.renderer << "\n";
    out << "OpenGL:    " << gpu.version << "\n";
    out << "GLSL:      " << gpu.glsl << "\n";
    out << "VRAM:      " << (gpu.vramMB > 0 ? std::to_string(gpu.vramMB) + " MB" : std::string("unknown")) << "\n";
    out << "S3TC:      " << (gpu.s3tc ? "yes" : "no") << "   anisotropic filtering: " << (gpu.anisotropic ? "yes" : "no") << "\n";
    out << "Legacy GPU: " << (gpu.legacy ? "yes" : "no") << "\n";
    out << "Suggested tier from GPU family: " << names[gpu.suggestedTier] << "\n";
    out << "Chosen tier: " << names[std::max(0, std::min(3, chosenTier))] << (fromCache ? " (from graphics.cfg)" : " (benchmark)") << "\n\n";
    if (!samples.empty()) {
        out << "Benchmark (full frame time at 1 view, ms):\n";
        for (const auto& s : samples)
            out << "  " << names[s.tier] << "  scale " << s.scale << "  ->  " << s.ms << " ms (" << (s.ms > 0 ? 1000.0f / s.ms : 0) << " fps)\n";
        out << "\n";
    }
    if (!passTimes.empty()) out << "GPU time per pass:\n" << passTimes << "\n";
    out << "Shader compiler messages:\n" << (shaderLog.empty() ? std::string("  (none)\n") : shaderLog) << "\n";
}
