// A stand-in for the model runner in tests (the model never runs in tests,
// DESIGN 3.6 item 8): speaks the runner's protocol (model/Runner.h) and
// answers from recorded answers. Argument 1: a JSON file
//   {"answers": [[<text the prompt holds>, <answer>], ...],
//    "delayMs": <n>, "loadError": <text>}
// The first entry whose text the prompt's plain pieces contain gives the
// answer; "@hang" never answers, "@exit" ends the process, "@error" answers
// {"ok":false}, "@raw:<text>" writes <text> as the whole reply line. A prompt
// no entry matches gets {"ok":false}.

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <cstdio>
#include <iostream>
#include <string>

namespace {

void reply(const QJsonObject &object)
{
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 2)
        return 2;
    QFile file(QString::fromLocal8Bit(argv[1]));
    if (!file.open(QIODevice::ReadOnly))
        return 3;
    const QJsonObject recorded = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray answers = recorded.value(QStringLiteral("answers")).toArray();
    const int delayMs = recorded.value(QStringLiteral("delayMs")).toInt();
    std::string line;
    while (std::getline(std::cin, line)) {
        const QJsonObject request = QJsonDocument::fromJson(QByteArray::fromStdString(line)).object();
        const QString op = request.value(QStringLiteral("op")).toString();
        if (op == QLatin1String("load")) {
            const QString error = recorded.value(QStringLiteral("loadError")).toString();
            reply(error.isEmpty() ? QJsonObject{{"ok", true}, {"ms", 1}} : QJsonObject{{"ok", false}, {"error", error}});
            continue;
        }
        QString prompt;
        for (const QJsonValue &piece : request.value(QStringLiteral("prompt")).toArray()) {
            if (!piece.toObject().value(QStringLiteral("special")).toBool())
                prompt += piece.toObject().value(QStringLiteral("text")).toString() + QLatin1Char('\n');
        }
        QString answer;
        for (const QJsonValue &entry : answers) {
            if (prompt.contains(entry.toArray().at(0).toString())) {
                answer = entry.toArray().at(1).toString();
                break;
            }
        }
        if (delayMs > 0)
            QThread::msleep(static_cast<unsigned long>(delayMs));
        if (answer == QLatin1String("@hang")) {
            QThread::sleep(3600);
            continue;
        }
        if (answer == QLatin1String("@exit"))
            return 0;
        if (answer.startsWith(QLatin1String("@raw:"))) {
            const QByteArray raw = answer.mid(5).toUtf8() + '\n';
            std::fwrite(raw.constData(), 1, static_cast<size_t>(raw.size()), stdout);
            std::fflush(stdout);
            continue;
        }
        if (answer.isEmpty() || answer == QLatin1String("@error")) {
            reply({{"ok", false}, {"error", "no recorded answer"}});
            continue;
        }
        reply({{"ok", true}, {"text", answer}, {"ms", delayMs}});
    }
    return 0;
}
