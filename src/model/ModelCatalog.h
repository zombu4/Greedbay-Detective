#pragma once

#include <QList>
#include <QString>
#include <QUrl>

#include <optional>

namespace app::model {

// The local models the app offers (DESIGN 3.6, item 3). Each is downloaded
// only when the user asks, from the maker's own repository at a fixed commit,
// and used only when its SHA-256 equals the value here.
enum class PromptFormat {
    // ChatML with Qwen3's thinking-off start of the answer: the model file's
    // own template adds "<think>\n\n</think>\n\n" after
    // "<|im_start|>assistant\n" when enable_thinking is false.
    Qwen3NoThinking,
    // Plain ChatML (<|im_start|>role ... <|im_end|>).
    ChatMl,
};

struct ModelInfo {
    QString id;           // "qwen3-4b"
    QString name;         // shown
    QString fileName;     // in the work folder's models/
    QUrl url;             // the maker's repository, pinned to a commit
    qint64 sizeBytes = 0;
    QString sha256;       // lower-case hex
    QString license;      // "Apache-2.0"
    QString licenseResource; // the license text as the repository has it
    // Memory the model needs while it works, as measured (0: not measured);
    // the app refuses to start it on a computer with less physical memory.
    qint64 memoryBytes = 0;
    QString memoryNote;   // how memoryBytes was found, or why it is not known
    PromptFormat format = PromptFormat::ChatMl;
    bool recommended = false;
};

const QList<ModelInfo> &catalog();
std::optional<ModelInfo> modelById(const QString &id);
QString defaultModelId();

// The computer's physical memory in bytes, 0 when it cannot be read.
qint64 physicalMemoryBytes();

// On x86-64 the engine is built with ggml's default instruction sets when
// GGML_NATIVE is off: SSE4.2, AVX, AVX2, FMA, F16C and BMI2 (cmake/deps.cmake).
// True when this processor (and the system, for the AVX registers) has them;
// else *missing names what is missing. Always true on ARM64.
bool processorSupportsEngine(QString *missing);

} // namespace app::model
