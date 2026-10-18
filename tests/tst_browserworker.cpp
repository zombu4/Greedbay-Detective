// Browser session lifecycle and containment, through the real worker process
// (the test build, browserworker_test, which may open local pages).
//
// This test does not link Qt WebEngine, like the app. It checks:
//   - lazy start: no browser process exists before a session is requested,
//     and the worker starts no QtWebEngineProcess before its first request
//   - a fetch of a local fixture page works through the worker; the page
//     sets a cookie (HTTP header and script) and writes local storage
//   - Linux only: a second fetch from a local https server with a self-made
//     certificate, so Chromium's certificate handling (NSS) runs inside the
//     containment check; the page is refused, shown, and ended by "quit"
//   - while the session runs, sign-out refuses (the session lock is held)
//   - after "quit" the worker exits normally and every process it started
//     (QtWebEngineProcess children included) is gone
//   - containment: listing HOME and the temporary folders before and after
//     shows no file added, changed or removed outside the work folder
//   - sign-out afterwards deletes the browser data without starting anything
// On Linux it also reports how the renderer processes were sandboxed.
#include "core/BrowserProtocol.h"
#include "core/BrowserWorker.h"
#include "core/WebEngineData.h"
#include "core/WorkFolder.h"
#include "support/FileSnapshot.h"
#include "support/ProcessList.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QMap>
#include <QSignalSpy>
#include <QProcess>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>
#include <optional>

#if defined(Q_OS_MACOS)
#  include <unistd.h>
#endif
#if defined(Q_OS_LINUX)
#  include <QSslCertificate>
#  include <QSslConfiguration>
#  include <QSslKey>
#  include <QSslServer>
#  include <QSslSocket>
#endif

using app::BrowserWorker;
using app::WebEngineData;
using app::WorkFolder;

namespace {

#if defined(Q_OS_MACOS)
// The per-user cache and temp folders macOS gives every process
// (confstr _CS_DARWIN_USER_CACHE_DIR / _CS_DARWIN_USER_TEMP_DIR).
QString darwinDir(int name)
{
    char buffer[1024];
    const size_t length = confstr(name, buffer, sizeof buffer);
    return length > 0 && length <= sizeof buffer ? QString::fromLocal8Bit(buffer) : QString();
}
#endif

QStringList scanRoots()
{
    QStringList roots{QDir::homePath(), QDir::tempPath()};
#if defined(Q_OS_MACOS)
    roots << darwinDir(_CS_DARWIN_USER_CACHE_DIR) << darwinDir(_CS_DARWIN_USER_TEMP_DIR);
#elif defined(Q_OS_LINUX)
    roots << QStringLiteral("/tmp") << QStringLiteral("/dev/shm");
#endif
    return distinctRoots(roots);
}

#if defined(Q_OS_LINUX)
QByteArray readSmall(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.read(65536) : QByteArray();
}

// "--type=renderer seccomp=2 no_new_privs=1 user_ns=own pid_ns=own" for a
// running process: its Chromium process type, the seccomp mode from
// /proc/<pid>/status (0 none, 1 strict, 2 filter), and whether its user and
// PID namespaces differ from this test's.
QString sandboxReport(qint64 pid)
{
    QByteArray cmdline = readSmall(QStringLiteral("/proc/%1/cmdline").arg(pid));
    cmdline.replace('\0', ' ');
    QString type = QStringLiteral("(no --type)");
    for (const QByteArray &part : cmdline.split(' ')) {
        if (part.startsWith("--type="))
            type = QString::fromLatin1(part);
    }
    QString seccomp = QStringLiteral("?");
    QString noNewPrivs = QStringLiteral("?");
    for (const QByteArray &line : readSmall(QStringLiteral("/proc/%1/status").arg(pid)).split('\n')) {
        if (line.startsWith("Seccomp:"))
            seccomp = QString::fromLatin1(line.mid(8).trimmed());
        else if (line.startsWith("NoNewPrivs:"))
            noNewPrivs = QString::fromLatin1(line.mid(11).trimmed());
    }
    auto ns = [pid](const char *name) {
        const QString mine = QFile::symLinkTarget(QStringLiteral("/proc/self/ns/%1").arg(QLatin1String(name)));
        const QString theirs = QFile::symLinkTarget(QStringLiteral("/proc/%1/ns/%2").arg(pid).arg(QLatin1String(name)));
        if (theirs.isEmpty())
            return QStringLiteral("unknown");
        return theirs == mine ? QStringLiteral("shared") : QStringLiteral("own");
    };
    return QStringLiteral("%1 seccomp=%2 no_new_privs=%3 user_ns=%4 pid_ns=%5")
        .arg(type, seccomp, noNewPrivs, ns("user"), ns("pid"));
}
#endif

} // namespace

class TestBrowserWorker : public QObject {
    Q_OBJECT

private:
    void onConnection();
    void recordDescendants();

    QTemporaryDir m_temp;
    std::optional<WorkFolder> m_folder;
    QTcpServer m_server;
    QByteArray m_page;
    int m_requests = 0;
    QStringList m_roots;
    QStringList m_excluded;
    FileSnapshot m_before;
    QMap<qint64, QString> m_seen;
    QStringList m_sandbox;
#if defined(Q_OS_LINUX)
    QSslServer m_tlsServer;
#endif

private slots:
    void initTestCase();
    void noBrowserProcessBeforeASession();
    void fetchThroughTheWorker();
    void nothingWrittenOutsideTheWorkFolder();
    void signOutDeletesBrowserDataWithoutABrowser();
};

void TestBrowserWorker::onConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            QByteArray request = socket->property("request").toByteArray() + socket->readAll();
            socket->setProperty("request", request);
            if (!request.contains("\r\n\r\n"))
                return;
            ++m_requests;
            const QList<QByteArray> requestLine = request.left(request.indexOf("\r\n")).split(' ');
            const QByteArray path = requestLine.size() >= 2 ? requestLine.at(1) : QByteArray();
            QByteArray response;
            if (path.startsWith("/sch/i.html")) {
                response = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/html; charset=utf-8\r\n"
                           "Set-Cookie: header-cookie=1; Max-Age=3600; Path=/\r\n"
                           "Cache-Control: max-age=3600\r\n"
                           "Content-Length: "
                           + QByteArray::number(m_page.size()) + "\r\nConnection: close\r\n\r\n" + m_page;
            } else {
                response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            }
            socket->write(response);
            socket->disconnectFromHost();
        });
    }
}

void TestBrowserWorker::recordDescendants()
{
    for (const ProcessInfo &process : descendants(QCoreApplication::applicationPid())) {
        if (m_seen.contains(process.pid))
            continue;
        m_seen.insert(process.pid, process.name);
#if defined(Q_OS_LINUX)
        m_sandbox << QStringLiteral("pid %1 %2: %3").arg(process.pid).arg(process.name, sandboxReport(process.pid));
#endif
    }
}

void TestBrowserWorker::initTestCase()
{
    QVERIFY(m_temp.isValid());
    QString error;
    m_folder = WorkFolder::open(m_temp.path() + QStringLiteral("/work"), TEST_APP_NAME, &error);
    QVERIFY2(m_folder, qPrintable(error));

    QFile page(QStringLiteral(":/fixtures/worker_page.html"));
    QVERIFY(page.open(QIODevice::ReadOnly));
    m_page = page.readAll();
    connect(&m_server, &QTcpServer::newConnection, this, &TestBrowserWorker::onConnection);
    QVERIFY(m_server.listen(QHostAddress::LocalHost));

#if defined(Q_OS_LINUX)
    // A certificate made now, before the first snapshot, in the test's
    // temporary folder (outside the work folder). Chromium will refuse it.
    QVERIFY2(QSslSocket::supportsSsl(), "Qt has no TLS backend here");
    const QString keyPath = m_temp.path() + QStringLiteral("/tls-key.pem");
    const QString certPath = m_temp.path() + QStringLiteral("/tls-cert.pem");
    QProcess openssl;
    openssl.start(QStringLiteral("openssl"),
                  {QStringLiteral("req"), QStringLiteral("-x509"), QStringLiteral("-newkey"), QStringLiteral("rsa:2048"),
                   QStringLiteral("-nodes"), QStringLiteral("-keyout"), keyPath, QStringLiteral("-out"), certPath,
                   QStringLiteral("-days"), QStringLiteral("1"), QStringLiteral("-subj"), QStringLiteral("/CN=127.0.0.1")});
    QVERIFY2(openssl.waitForFinished(60000) && openssl.exitStatus() == QProcess::NormalExit && openssl.exitCode() == 0,
             qPrintable(QString::fromLocal8Bit(openssl.readAllStandardError())));
    QFile keyFile(keyPath);
    QFile certFile(certPath);
    QVERIFY(keyFile.open(QIODevice::ReadOnly) && certFile.open(QIODevice::ReadOnly));
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setPrivateKey(QSslKey(keyFile.readAll(), QSsl::Rsa));
    tls.setLocalCertificate(QSslCertificate(certFile.readAll()));
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_tlsServer.setSslConfiguration(tls);
    QVERIFY(m_tlsServer.listen(QHostAddress::LocalHost));
#endif

    m_roots = scanRoots();
    m_excluded = {canonical(m_folder->path())};
    const QString testing = canonical(QStringLiteral(TEST_CTEST_DIR));
    if (!testing.isEmpty())
        m_excluded << testing;
    qInfo().noquote() << "containment scan roots:" << m_roots.join(QStringLiteral(" | "));
    qInfo().noquote() << "excluded (work folder, ctest's own log folder):" << m_excluded.join(QStringLiteral(" | "));
    QElapsedTimer clock;
    clock.start();
    m_before = takeSnapshot(m_roots, m_excluded);
    qInfo() << "snapshot before:" << m_before.entries.size() << "entries in" << clock.elapsed() << "ms";
}

void TestBrowserWorker::noBrowserProcessBeforeASession()
{
    QVERIFY(descendants(QCoreApplication::applicationPid()).isEmpty());
}

void TestBrowserWorker::fetchThroughTheWorker()
{
    QTimer poll;
    connect(&poll, &QTimer::timeout, this, &TestBrowserWorker::recordDescendants);
    poll.start(100);

    BrowserWorker worker(*m_folder, QStringLiteral(TEST_WORKER_PATH));
    QSignalSpy ready(&worker, &BrowserWorker::ready);
    QSignalSpy replied(&worker, &BrowserWorker::replied);
    QSignalSpy failed(&worker, &BrowserWorker::failed);
    QSignalSpy finished(&worker, &BrowserWorker::finished);
    QString error;
    QVERIFY2(worker.start(&error), qPrintable(error));
    QVERIFY2(ready.wait(BrowserWorker::kStartTimeoutMs), "the worker did not report ready");
    // The worker runs, but no browser profile exists before the first
    // request: no QtWebEngineProcess yet.
    for (const ProcessInfo &process : descendants(QCoreApplication::applicationPid()))
        QVERIFY2(!process.name.startsWith(QLatin1String("QtWebEngineProc")),
                 "a QtWebEngineProcess runs before the first request");

    const QString url = QStringLiteral("http://127.0.0.1:%1/sch/i.html?_nkw=test+meter&_ipg=240").arg(m_server.serverPort());
    const int id = worker.request(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("fetch")},
                                              {QStringLiteral("kind"), QStringLiteral("activeSearch")},
                                              {QStringLiteral("url"), url}});
    QVERIFY(id > 0);
    QCOMPARE(worker.request(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("signIn")}}), -1); // one at a time
    QVERIFY2(replied.wait(120000), "no reply to the fetch");
    recordDescendants();
    QCOMPARE(replied.at(0).at(0).toInt(), id);
    const QJsonObject reply = replied.at(0).at(1).toJsonObject();
    QCOMPARE(reply.value(QStringLiteral("status")).toString(), QStringLiteral("ok"));
    const QJsonObject result = reply.value(QStringLiteral("result")).toObject();
    QCOMPARE(result.value(QStringLiteral("totalResults")).toInt(), 1);
    const QJsonArray items = result.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("itemId")).toString(), QStringLiteral("100000000040"));
    QCOMPARE(items.at(0).toObject().value(QStringLiteral("price")).toObject().value(QStringLiteral("minCents")).toInt(), 999);
    QVERIFY(m_requests >= 1);

    // The session holds the lock: sign-out refuses and deletes nothing.
    QVERIFY(!WebEngineData::clear(*m_folder, &error));
    QVERIFY(error.contains(QStringLiteral("browser session is running")));

    const QList<ProcessInfo> during = descendants(QCoreApplication::applicationPid());
    QVERIFY(!during.isEmpty());

    int expectedReplies = 2; // the fetch and the quit
#if defined(Q_OS_LINUX)
    QSignalSpy handshakes(&m_tlsServer, &QSslServer::startedEncryptionHandshake);
    QSignalSpy tlsErrors(&m_tlsServer, &QSslServer::errorOccurred);
    const QString tlsUrl = QStringLiteral("https://127.0.0.1:%1/sch/i.html?_nkw=test+meter&_ipg=240").arg(m_tlsServer.serverPort());
    const int tlsId = worker.request(QJsonObject{{QStringLiteral("cmd"), QStringLiteral("fetch")},
                                                 {QStringLiteral("kind"), QStringLiteral("activeSearch")},
                                                 {QStringLiteral("url"), tlsUrl}});
    QVERIFY(tlsId > 0);
    // Chromium checks the certificate during the handshake and ends the
    // connection; the worker then shows its error page and waits for the
    // user, so "quit" below ends that request ("failed: stopped").
    QVERIFY2(!handshakes.isEmpty() || handshakes.wait(60000), "no TLS handshake from the browser");
    QVERIFY2(!tlsErrors.isEmpty() || tlsErrors.wait(60000), "the TLS connection did not end");
    QCOMPARE(replied.size(), 1); // still waiting for the user
    expectedReplies = 3;
#endif

    worker.stop();
    QVERIFY2(finished.wait(BrowserWorker::kStopTimeoutMs + 10000), "the worker did not exit");
    poll.stop();
    QCOMPARE(finished.at(0).at(0).toBool(), true);
    QCOMPARE(finished.at(0).at(1).toInt(), 0);
    QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(0).toString()));
    // Every request was answered, the quit request last.
    QCOMPARE(replied.size(), expectedReplies);
#if defined(Q_OS_LINUX)
    QCOMPARE(replied.at(1).at(0).toInt(), tlsId);
    QCOMPARE(replied.at(1).at(1).toJsonObject().value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
    QCOMPARE(replied.at(1).at(1).toJsonObject().value(QStringLiteral("error")).toString(), QStringLiteral("stopped"));
#endif
    QCOMPARE(replied.last().at(1).toJsonObject().value(QStringLiteral("status")).toString(), QStringLiteral("ok"));

    QStringList seen;
    bool sawWebEngine = false;
    for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it) {
        seen << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
        sawWebEngine = sawWebEngine || it.value().startsWith(QLatin1String("QtWebEngineProc"));
    }
    qInfo().noquote() << "processes seen during the session:" << seen.join(QStringLiteral(", "));
    for (const QString &line : std::as_const(m_sandbox))
        qInfo().noquote() << "linux sandbox:" << line;
    QVERIFY2(sawWebEngine, "no QtWebEngineProcess was seen, so the lifecycle check would prove nothing");

    // Every process of the session ends; give the operating system a moment
    // to finish them after the worker's exit.
    const bool allGone = QTest::qWaitFor([this]() {
        for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it) {
            if (processRunning(it.key()))
                return false;
        }
        return true;
    }, 15000);
    QStringList remaining;
    for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it) {
        if (processRunning(it.key()))
            remaining << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    }
    QVERIFY2(allGone, qPrintable(QStringLiteral("still running after teardown: ") + remaining.join(QStringLiteral(", "))));
    QVERIFY(descendants(QCoreApplication::applicationPid()).isEmpty());
    qInfo() << "after teardown: none of the" << m_seen.size() << "session processes runs";

    // The browser stored its data in the work folder.
    bool cookies = false;
    QDirIterator it(m_folder->webEngineStorageDir(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (it.next().endsWith(QLatin1String("/Cookies")))
            cookies = true;
    }
    QVERIFY2(cookies, "no Cookies file in webengine/storage");
    QVERIFY(QFileInfo(m_folder->webEngineStorageDir() + QStringLiteral("/Local Storage")).isDir());
}

void TestBrowserWorker::nothingWrittenOutsideTheWorkFolder()
{
    QElapsedTimer clock;
    clock.start();
    const FileSnapshot after = takeSnapshot(m_roots, m_excluded);
    qInfo() << "snapshot after:" << after.entries.size() << "entries in" << clock.elapsed() << "ms;"
            << "unreadable folders skipped:" << after.unreadableDirs;

    int inside = 0;
    QDirIterator it(m_folder->path(), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++inside;
    }
    qInfo() << "entries inside the work folder:" << inside;

    const QStringList outside = snapshotDifference(m_before, after);
    for (const QString &line : outside)
        qInfo().noquote() << "outside the work folder:" << line;
    QVERIFY2(outside.isEmpty(),
             qPrintable(QStringLiteral("%1 change(s) outside the work folder, first: %2")
                            .arg(outside.size())
                            .arg(outside.isEmpty() ? QString() : outside.first())));
}

void TestBrowserWorker::signOutDeletesBrowserDataWithoutABrowser()
{
    QString error;
    QVERIFY2(WebEngineData::clear(*m_folder, &error), qPrintable(error));
    QVERIFY(descendants(QCoreApplication::applicationPid()).isEmpty());
    const QDir::Filters all = QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot;
    QVERIFY(QDir(m_folder->webEngineStorageDir()).isEmpty(all));
    QVERIFY(QDir(m_folder->webEngineCacheDir()).isEmpty(all));
    QVERIFY(QDir(m_folder->webEngineDownloadsDir()).isEmpty(all));
}

QTEST_GUILESS_MAIN(TestBrowserWorker)
#include "tst_browserworker.moc"
