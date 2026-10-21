#include "model/ModelCatalog.h"

#if defined(Q_PROCESSOR_X86_64)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

#if defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace app::model {

const QList<ModelInfo> &catalog()
{
    // Sizes and SHA-256 values: the repositories' file lists (Hugging Face
    // API, LFS "sha256"), both checked against the downloaded files
    // 2026-10-20. Memory: Qwen3-4B measured 2026-10-20 with the engine's own
    // server program on Apple Metal: 3,104 MB resident after loading, 3,209 MB
    // at most (GPU buffers on other systems are not counted there).
    static const QList<ModelInfo> models{
        {QStringLiteral("qwen3-4b"),
         QStringLiteral("Qwen3-4B (Q4_K_M)"),
         QStringLiteral("Qwen3-4B-Q4_K_M.gguf"),
         QUrl(QStringLiteral("https://huggingface.co/Qwen/Qwen3-4B-GGUF/resolve/"
                             "bc640142c66e1fdd12af0bd68f40445458f3869b/Qwen3-4B-Q4_K_M.gguf")),
         2497280256LL,
         QStringLiteral("7485fe6f11af29433bc51cab58009521f205840f5b4ae3a32fa7f92e8534fdf5"),
         QStringLiteral("Apache-2.0"),
         QStringLiteral(":/models/Qwen3-4B-LICENSE.txt"),
         3209LL * 1024 * 1024,
         QStringLiteral("measured: about 3.2 GB while it works"),
         PromptFormat::Qwen3NoThinking,
         true},
        {QStringLiteral("qwen2.5-1.5b"),
         QStringLiteral("Qwen2.5-1.5B-Instruct (Q4_K_M)"),
         QStringLiteral("qwen2.5-1.5b-instruct-q4_k_m.gguf"),
         QUrl(QStringLiteral("https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/"
                             "91cad51170dc346986eccefdc2dd33a9da36ead9/qwen2.5-1.5b-instruct-q4_k_m.gguf")),
         1117320736LL,
         QStringLiteral("6a1a2eb6d15622bf3c96857206351ba97e1af16c30d7a74ee38970e434e9407e"),
         QStringLiteral("Apache-2.0"),
         QStringLiteral(":/models/Qwen2.5-1.5B-LICENSE.txt"),
         0,
         QStringLiteral("not measured; at least the file size"),
         PromptFormat::ChatMl,
         false},
    };
    return models;
}

std::optional<ModelInfo> modelById(const QString &id)
{
    for (const ModelInfo &info : catalog()) {
        if (info.id == id)
            return info;
    }
    return std::nullopt;
}

QString defaultModelId() { return QStringLiteral("qwen3-4b"); }

qint64 physicalMemoryBytes()
{
#if defined(Q_OS_MACOS)
    // sysctl(3): hw.memsize, the physical memory in bytes (64-bit).
    int64_t bytes = 0;
    size_t length = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &length, nullptr, 0) != 0 || length != sizeof(bytes))
        return 0;
    return static_cast<qint64>(bytes);
#elif defined(Q_OS_WIN)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (!GlobalMemoryStatusEx(&status))
        return 0;
    return static_cast<qint64>(status.ullTotalPhys);
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages <= 0 || pageSize <= 0)
        return 0;
    return static_cast<qint64>(pages) * static_cast<qint64>(pageSize);
#endif
}

bool processorSupportsEngine(QString *missing)
{
    missing->clear();
#if defined(Q_PROCESSOR_X86_64)
    // CPUID leaf 1: ECX bit 12 FMA, 20 SSE4.2, 27 OSXSAVE, 28 AVX, 29 F16C;
    // leaf 7 subleaf 0: EBX bit 5 AVX2, bit 8 BMI2; XGETBV(0) bits 1 and 2:
    // the system saves the SSE and AVX registers (Intel SDM, vol. 2A CPUID,
    // vol. 1 13.5).
    unsigned int leaf1[4] = {0, 0, 0, 0};
    unsigned int leaf7[4] = {0, 0, 0, 0};
    unsigned long long xcr0 = 0;
#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs, 0);
    const int maxLeaf = regs[0];
    __cpuid(regs, 1);
    for (int i = 0; i < 4; ++i)
        leaf1[i] = static_cast<unsigned int>(regs[i]);
    if (maxLeaf >= 7) {
        __cpuidex(regs, 7, 0);
        for (int i = 0; i < 4; ++i)
            leaf7[i] = static_cast<unsigned int>(regs[i]);
    }
    const bool osxsave = (leaf1[2] >> 27) & 1u;
    if (osxsave)
        xcr0 = _xgetbv(0);
#else
    __get_cpuid(1, &leaf1[0], &leaf1[1], &leaf1[2], &leaf1[3]);
    __get_cpuid_count(7, 0, &leaf7[0], &leaf7[1], &leaf7[2], &leaf7[3]);
    const bool osxsave = (leaf1[2] >> 27) & 1u;
    if (osxsave) {
        unsigned int low = 0;
        unsigned int high = 0;
        __asm__("xgetbv" : "=a"(low), "=d"(high) : "c"(0));
        xcr0 = (static_cast<unsigned long long>(high) << 32) | low;
    }
#endif
    QStringList absent;
    if (!((leaf1[2] >> 20) & 1u))
        absent.append(QStringLiteral("SSE4.2"));
    if (!((leaf1[2] >> 28) & 1u) || !osxsave || (xcr0 & 6u) != 6u)
        absent.append(QStringLiteral("AVX"));
    if (!((leaf7[1] >> 5) & 1u))
        absent.append(QStringLiteral("AVX2"));
    if (!((leaf1[2] >> 12) & 1u))
        absent.append(QStringLiteral("FMA"));
    if (!((leaf1[2] >> 29) & 1u))
        absent.append(QStringLiteral("F16C"));
    if (!((leaf7[1] >> 8) & 1u))
        absent.append(QStringLiteral("BMI2"));
    *missing = absent.join(QStringLiteral(", "));
    return absent.isEmpty();
#else
    return true;
#endif
}

} // namespace app::model
