#include "model/Runner.h"

#include "model/Prompts.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <llama.h>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace app::model {

namespace {

constexpr size_t kMaxRequestBytes = 1024 * 1024;
constexpr int kMaxAnswerTokens = 160;
constexpr uint32_t kContextTokens = 4096;
constexpr uint32_t kBatchTokens = 512;
constexpr size_t kHashChunk = 4 * 1024 * 1024;

void dropLog(enum ggml_log_level, const char *, void *) {}

struct Engine {
    FILE *file = nullptr; // the model file, hashed and loaded through this one handle
    llama_model *model = nullptr;
    llama_context *context = nullptr;
    const llama_vocab *vocab = nullptr;
    std::vector<llama_token> cached; // the tokens in the engine's memory (sequence 0)

    void unload()
    {
        if (context)
            llama_free(context);
        if (model)
            llama_model_free(model);
        if (file)
            std::fclose(file);
        context = nullptr;
        model = nullptr;
        file = nullptr;
        vocab = nullptr;
        cached.clear();
    }
    ~Engine() { unload(); }
};

void reply(const QJsonObject &object)
{
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

QJsonObject failure(const QString &error) { return {{"ok", false}, {"error", error}}; }

FILE *openFile(const QString &path)
{
#ifdef Q_OS_WIN
    FILE *file = nullptr;
    return _wfopen_s(&file, reinterpret_cast<const wchar_t *>(path.utf16()), L"rb") == 0 ? file : nullptr;
#else
    return std::fopen(QFile::encodeName(path).constData(), "rb");
#endif
}

QJsonObject load(Engine &engine, const QJsonObject &request)
{
    QElapsedTimer timer;
    timer.start();
    engine.unload();
    const QString path = request.value(QStringLiteral("path")).toString();
    const QString sha256 = request.value(QStringLiteral("sha256")).toString();
    const QFileInfo info(path);
    if (path.isEmpty() || info.isSymLink() || !info.isFile())
        return failure(QStringLiteral("the model file is missing or is a link"));
    engine.file = openFile(path);
    if (!engine.file)
        return failure(QStringLiteral("the model file cannot be opened"));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::vector<char> buffer(kHashChunk);
    for (;;) {
        const size_t read = std::fread(buffer.data(), 1, buffer.size(), engine.file);
        if (read > 0)
            hash.addData(QByteArrayView(buffer.data(), static_cast<qsizetype>(read)));
        if (read < buffer.size())
            break;
    }
    if (std::ferror(engine.file)) {
        engine.unload();
        return failure(QStringLiteral("the model file cannot be read"));
    }
    if (QString::fromLatin1(hash.result().toHex()) != sha256.toLower()) {
        engine.unload();
        return failure(QStringLiteral("the model file's SHA-256 is not the pinned one; it is not used"));
    }
    std::rewind(engine.file);

    llama_model_params modelParams = llama_model_default_params();
    modelParams.n_gpu_layers = request.value(QStringLiteral("gpu")).toBool() ? 999 : 0;
    engine.model = llama_model_load_from_file_ptr(engine.file, modelParams);
    if (!engine.model) {
        engine.unload();
        return failure(QStringLiteral("the engine could not load the model"));
    }
    llama_context_params contextParams = llama_context_default_params();
    const int threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8);
    contextParams.n_ctx = kContextTokens;
    contextParams.n_batch = kBatchTokens;
    contextParams.n_threads = threads;
    contextParams.n_threads_batch = threads;
    contextParams.no_perf = true;
    engine.context = llama_init_from_model(engine.model, contextParams);
    if (!engine.context) {
        engine.unload();
        return failure(QStringLiteral("the engine could not make a context"));
    }
    engine.vocab = llama_model_get_vocab(engine.model);
    return {{"ok", true}, {"ms", static_cast<qint64>(timer.elapsed())}};
}

bool tokenize(const llama_vocab *vocab, const QByteArray &text, bool addSpecial, bool parseSpecial,
              std::vector<llama_token> *out)
{
    std::vector<llama_token> tokens(static_cast<size_t>(text.size()) + 8);
    int32_t count = llama_tokenize(vocab, text.constData(), static_cast<int32_t>(text.size()), tokens.data(),
                                   static_cast<int32_t>(tokens.size()), addSpecial, parseSpecial);
    if (count < 0 && count != INT32_MIN) {
        tokens.resize(static_cast<size_t>(-count));
        count = llama_tokenize(vocab, text.constData(), static_cast<int32_t>(text.size()), tokens.data(),
                               static_cast<int32_t>(tokens.size()), addSpecial, parseSpecial);
    }
    if (count < 0)
        return false;
    out->insert(out->end(), tokens.begin(), tokens.begin() + count);
    return true;
}

// Decodes `tokens` from position `from` on (sequence 0); logits only for the
// last one.
bool decode(Engine &engine, const std::vector<llama_token> &tokens, size_t from)
{
    for (size_t start = from; start < tokens.size(); start += kBatchTokens) {
        const size_t count = std::min<size_t>(kBatchTokens, tokens.size() - start);
        llama_batch batch = llama_batch_init(static_cast<int32_t>(count), 0, 1);
        for (size_t i = 0; i < count; ++i) {
            batch.token[i] = tokens[start + i];
            batch.pos[i] = static_cast<llama_pos>(start + i);
            batch.n_seq_id[i] = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i] = start + i + 1 == tokens.size() ? 1 : 0;
        }
        batch.n_tokens = static_cast<int32_t>(count);
        const int32_t result = llama_decode(engine.context, batch);
        llama_batch_free(batch);
        if (result != 0)
            return false;
    }
    return true;
}

QJsonObject generate(Engine &engine, const QJsonObject &request)
{
    if (!engine.context)
        return failure(QStringLiteral("no model loaded"));
    QElapsedTimer timer;
    timer.start();
    const QString grammarName = request.value(QStringLiteral("grammar")).toString();
    if (grammarName != QLatin1String("identity") && grammarName != QLatin1String("sort"))
        return failure(QStringLiteral("unknown grammar"));
    const QByteArray grammar = grammarText(grammarName == QLatin1String("identity") ? Grammar::Identity : Grammar::Sort).toUtf8();

    std::vector<llama_token> tokens;
    bool first = true;
    for (const QJsonValue &value : request.value(QStringLiteral("prompt")).toArray()) {
        const QJsonObject piece = value.toObject();
        // Only the chat format's own markers are read as special tokens.
        if (!tokenize(engine.vocab, piece.value(QStringLiteral("text")).toString().toUtf8(), first,
                      piece.value(QStringLiteral("special")).toBool(), &tokens))
            return failure(QStringLiteral("the prompt cannot be tokenized"));
        first = false;
    }
    if (tokens.empty() || tokens.size() + kMaxAnswerTokens > kContextTokens)
        return failure(QStringLiteral("the prompt is empty or too long"));

    // The prompt's common start stays in memory; the rest is removed and
    // decoded (at least the last token, for its logits).
    size_t keep = 0;
    while (keep < engine.cached.size() && keep < tokens.size() && engine.cached[keep] == tokens[keep])
        ++keep;
    keep = std::min(keep, tokens.size() - 1);
    llama_memory_t memory = llama_get_memory(engine.context);
    if (!llama_memory_seq_rm(memory, 0, static_cast<llama_pos>(keep), -1)) {
        llama_memory_clear(memory, true);
        keep = 0;
    }
    engine.cached.assign(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(keep));
    if (!decode(engine, tokens, keep)) {
        llama_memory_clear(memory, true);
        engine.cached.clear();
        return failure(QStringLiteral("the engine could not read the prompt"));
    }
    engine.cached = tokens;

    llama_sampler *chain = llama_sampler_chain_init(llama_sampler_chain_default_params());
    llama_sampler *grammarSampler = llama_sampler_init_grammar(engine.vocab, grammar.constData(), "root");
    if (!grammarSampler) {
        llama_sampler_free(chain);
        return failure(QStringLiteral("the grammar cannot be read"));
    }
    llama_sampler_chain_add(chain, grammarSampler);
    llama_sampler_chain_add(chain, llama_sampler_init_greedy());

    std::string answer;
    bool ended = false;
    for (int step = 0; step < kMaxAnswerTokens; ++step) {
        const llama_token token = llama_sampler_sample(chain, engine.context, -1);
        if (llama_vocab_is_eog(engine.vocab, token)) {
            ended = true;
            break;
        }
        char piece[256];
        const int32_t length = llama_token_to_piece(engine.vocab, token, piece, sizeof(piece), 0, false);
        if (length < 0) {
            llama_sampler_free(chain);
            return failure(QStringLiteral("an answer token cannot be read"));
        }
        answer.append(piece, static_cast<size_t>(length));
        engine.cached.push_back(token);
        if (!decode(engine, engine.cached, engine.cached.size() - 1)) {
            llama_sampler_free(chain);
            llama_memory_clear(memory, true);
            engine.cached.clear();
            return failure(QStringLiteral("the engine stopped while answering"));
        }
    }
    llama_sampler_free(chain);
    if (!ended)
        return failure(QStringLiteral("the answer did not end"));
    return {{"ok", true}, {"text", QString::fromStdString(answer)}, {"ms", static_cast<qint64>(timer.elapsed())}};
}

} // namespace

int runModelRunner()
{
    llama_log_set(dropLog, nullptr);
    llama_backend_init();
    {
        Engine engine;
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.size() > kMaxRequestBytes) {
                reply(failure(QStringLiteral("request too long")));
                continue;
            }
            const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromStdString(line));
            const QString op = document.object().value(QStringLiteral("op")).toString();
            if (op == QLatin1String("load"))
                reply(load(engine, document.object()));
            else if (op == QLatin1String("generate"))
                reply(generate(engine, document.object()));
            else
                reply(failure(QStringLiteral("unknown request")));
        }
    }
    llama_backend_free();
    return 0;
}

} // namespace app::model
