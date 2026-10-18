// The app <-> browser worker line protocol (src/core/BrowserProtocol).
#include "core/BrowserProtocol.h"

#include <QTest>

using namespace app::browserprotocol;

class TestBrowserProtocol : public QObject {
    Q_OBJECT

private slots:
    void encodeIsOneLine();
    void splitsAcrossChunks();
    void severalMessagesInOneChunk();
    void notAnObjectBreaksTheReader();
    void tooLongLineBreaksTheReader();
};

void TestBrowserProtocol::encodeIsOneLine()
{
    const QByteArray line = encode(QJsonObject{{QStringLiteral("text"), QStringLiteral("a\nb")}});
    QVERIFY(line.endsWith('\n'));
    QCOMPARE(line.count('\n'), 1);
}

void TestBrowserProtocol::splitsAcrossChunks()
{
    const QByteArray line = encode(QJsonObject{{QStringLiteral("id"), 7}});
    LineReader reader;
    QString error;
    reader.append(line.left(3));
    QVERIFY(!reader.next(&error).has_value());
    QVERIFY(error.isEmpty());
    reader.append(line.mid(3));
    const std::optional<QJsonObject> message = reader.next(&error);
    QVERIFY(message.has_value());
    QCOMPARE(message->value(QStringLiteral("id")).toInt(), 7);
    QVERIFY(!reader.next(&error).has_value());
    QVERIFY(error.isEmpty());
}

void TestBrowserProtocol::severalMessagesInOneChunk()
{
    LineReader reader;
    reader.append(encode(QJsonObject{{QStringLiteral("id"), 1}}) + encode(QJsonObject{{QStringLiteral("id"), 2}}));
    QString error;
    QCOMPARE(reader.next(&error)->value(QStringLiteral("id")).toInt(), 1);
    QCOMPARE(reader.next(&error)->value(QStringLiteral("id")).toInt(), 2);
    QVERIFY(!reader.next(&error).has_value());
}

void TestBrowserProtocol::notAnObjectBreaksTheReader()
{
    LineReader reader;
    reader.append("[1,2]\n" + encode(QJsonObject{{QStringLiteral("id"), 1}}));
    QString error;
    QVERIFY(!reader.next(&error).has_value());
    QVERIFY(!error.isEmpty());
    // Broken for good: later lines are not read.
    error.clear();
    QVERIFY(!reader.next(&error).has_value());
    reader.append(encode(QJsonObject{{QStringLiteral("id"), 2}}));
    QVERIFY(!reader.next(&error).has_value());
}

void TestBrowserProtocol::tooLongLineBreaksTheReader()
{
    LineReader reader;
    reader.append(QByteArray(kMaxLineBytes + 1, 'x'));
    QString error;
    QVERIFY(!reader.next(&error).has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TestBrowserProtocol)
#include "tst_browserprotocol.moc"
