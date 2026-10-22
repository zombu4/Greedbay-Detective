// The shared seller list: hybrid cryptography, the trust chain, reports, the
// server's checks and limits, and the app's client against a local server,
// over plain HTTP and over the server's own HTTPS.
// Everything is synthetic: made-up seller ids, item numbers and host names
// (example.org is reserved for examples, RFC 2606), test certificates for
// 127.0.0.1 only.

#include "HttpServer.h"
#include "KeyFiles.h"
#include "ServerStore.h"
#include "Service.h"
#include "sellerlist/Crypto.h"
#include "sellerlist/ListClient.h"
#include "sellerlist/Protocol.h"
#include "sellerlist/Trust.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>

using namespace app::sellerlist;
using sellerserver::HttpServer;
using sellerserver::ServerStore;
using sellerserver::Service;

namespace {

const QString kHost = QStringLiteral("list.example.org");

struct Chain {
    SignKeys master;
    SignKeys serverSign;
    BoxKeys serverBox;
    Authorization authorization;
    QJsonObject authorizationDoc;
    MasterInfo info;
};

Chain makeChain(const QDateTime &now, qint64 sequence = 1)
{
    Chain chain;
    chain.master = *newSignKeys();
    chain.serverSign = *newSignKeys();
    chain.serverBox = *newBoxKeys();
    chain.authorization = {chain.serverSign.pub, chain.serverBox.pub, kHost, sequence, now.addDays(-1), now.addDays(30)};
    chain.authorizationDoc = *signDocument(chain.authorization.toBody(), chain.master);
    chain.info = {chain.master.pub, kHost};
    return chain;
}

QJsonObject bundle(const Chain &chain, const SellerList &list, const SignKeys &signer, const QJsonArray &revocations = {})
{
    return QJsonObject{{QStringLiteral("authorization"), chain.authorizationDoc},
                       {QStringLiteral("revocations"), revocations},
                       {QStringLiteral("list"), *signDocument(list.toBody(), signer)}};
}

Report signedReport(const QString &seller, const QString &item, const InstallKey &key, const QDateTime &created)
{
    Report report{seller, QStringLiteral("not_as_described"), QStringLiteral("Arrived broken, seller stopped answering."),
                  item, QStringLiteral("2026-09"), created, key.pub, {}};
    report.installSignature = installSign(report.signedBytes(), key);
    return report;
}

// One raw HTTP exchange with a local server.
QByteArray rawExchange(quint16 port, const QByteArray &request)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    if (!socket.waitForConnected(5000))
        return {};
    socket.write(request);
    QByteArray answer;
    while (socket.state() == QAbstractSocket::ConnectedState && socket.waitForReadyRead(5000))
        answer += socket.readAll();
    return answer + socket.readAll();
}

} // namespace

class TestSellerList : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void hybridSignatures();
    void hybridEnvelope();
    void secretAtRest();
    void trustChain();
    void reportChecks();
    void httpLimits();
    void serverHostNames();
    void clientNotSetUp();
    void httpsEndToEnd();
    void tlsRenewal();
    void serviceEndToEnd();
    void serviceLimits();
};

void TestSellerList::initTestCase() { QVERIFY(initialize()); }

void TestSellerList::hybridSignatures()
{
    const SignKeys keys = *newSignKeys();
    QCOMPARE(keys.pub.ed25519.size(), 32);
    QCOMPARE(keys.pub.mldsa65.size(), 1952);
    const QByteArray message = "the list body";
    const HybridSignature signature = *sign(message, keys);
    QCOMPARE(signature.ed25519.size(), 64);
    QCOMPARE(signature.mldsa65.size(), 3309);
    QVERIFY(verify(message, signature, keys.pub));
    QVERIFY(!verify(message + "!", signature, keys.pub));

    // Both halves must verify: a broken half fails the whole signature.
    HybridSignature brokenEd = signature;
    brokenEd.ed25519[0] = static_cast<char>(brokenEd.ed25519.at(0) ^ 1);
    QVERIFY(!verify(message, brokenEd, keys.pub));
    HybridSignature brokenMl = signature;
    brokenMl.mldsa65[100] = static_cast<char>(brokenMl.mldsa65.at(100) ^ 1);
    QVERIFY(!verify(message, brokenMl, keys.pub));

    // A half from another key fails too.
    const SignKeys other = *newSignKeys();
    const HybridSignature otherSignature = *sign(message, other);
    QVERIFY(!verify(message, HybridSignature{signature.ed25519, otherSignature.mldsa65}, keys.pub));
    QVERIFY(!verify(message, HybridSignature{otherSignature.ed25519, signature.mldsa65}, keys.pub));

    // JSON form round trip.
    const std::optional<HybridSignature> parsed = HybridSignature::fromJson(signature.toJson());
    QVERIFY(parsed);
    QVERIFY(verify(message, *parsed, keys.pub));
    QCOMPARE(*SignPublic::fromJson(keys.pub.toJson()), keys.pub);
}

void TestSellerList::hybridEnvelope()
{
    const BoxKeys keys = *newBoxKeys();
    const QByteArray message = R"({"type":"report"})";
    const QByteArray envelope = *seal(message, keys.pub, QStringLiteral("report"));
    QCOMPARE(envelope.size(), 1 + 32 + 1088 + 24 + message.size() + 16);
    QCOMPARE(envelope.at(0), char(1));
    QCOMPARE(*open(envelope, keys, QStringLiteral("report")), message);

    QVERIFY(!open(envelope, keys, QStringLiteral("dispute"))); // the label is bound in
    const BoxKeys other = *newBoxKeys();
    QVERIFY(!open(envelope, other, QStringLiteral("report")));
    // Any changed byte fails: version, ephemeral key, KEM ciphertext, nonce, ciphertext.
    for (const qsizetype at : {qsizetype(0), qsizetype(5), qsizetype(40), qsizetype(1130), envelope.size() - 1}) {
        QByteArray changed = envelope;
        changed[at] = static_cast<char>(changed.at(at) ^ 1);
        QVERIFY2(!open(changed, keys, QStringLiteral("report")), qPrintable(QString::number(at)));
    }
    QVERIFY(!open(envelope.left(100), keys, QStringLiteral("report")));
    // Two seals of the same message differ (fresh ephemeral key and nonce).
    QVERIFY(*seal(message, keys.pub, QStringLiteral("report")) != envelope);
}

void TestSellerList::secretAtRest()
{
    const QByteArray secret = randomBytes(64);
    const QJsonObject file = *encryptSecret(secret, "a long enough passphrase");
    QCOMPARE(file.value(QStringLiteral("format")).toInt(), 1);
    QVERIFY(!QJsonDocument(file).toJson().contains(secret.toBase64()));
    QCOMPARE(*decryptSecret(file, "a long enough passphrase"), secret);
    QVERIFY(!decryptSecret(file, "another long passphrase"));

    // Weakened parameters in the file are refused, not used.
    QJsonObject weak = file;
    weak.insert(QStringLiteral("opslimit"), 1);
    QVERIFY(!decryptSecret(weak, "a long enough passphrase"));

    // Key files through the server's helpers: new file only, owner-only.
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("k.json"));
    const SignKeys keys = *newSignKeys();
    QString error;
    QVERIFY2(sellerserver::saveSignKeys(path, keys, "a long enough passphrase", &error), qPrintable(error));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(path) & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions());
#endif
    QVERIFY(!sellerserver::saveSignKeys(path, keys, "a long enough passphrase", &error)); // never overwritten
    const std::optional<SignKeys> loaded = sellerserver::loadSignKeys(path, "a long enough passphrase", &error);
    QVERIFY(loaded);
    QCOMPARE(loaded->pub, keys.pub);
    QCOMPARE(loaded->ed25519Secret, keys.ed25519Secret);
    QVERIFY(!sellerserver::loadSignKeys(path, "the wrong passphrase!", &error));

    // A passphrase file others can read is refused.
    const QString passFile = dir.filePath(QStringLiteral("pass"));
    QFile pass(passFile);
    QVERIFY(pass.open(QIODevice::WriteOnly));
    pass.write("a long enough passphrase\n");
    pass.close();
    QVERIFY(QFile::setPermissions(passFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadOther));
#ifndef Q_OS_WIN
    QVERIFY(!sellerserver::readPassphrase(passFile, false, &error));
#endif
    QVERIFY(QFile::setPermissions(passFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    QCOMPARE(*sellerserver::readPassphrase(passFile, false, &error), QByteArray("a long enough passphrase"));
}

void TestSellerList::trustChain()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const Chain chain = makeChain(now);
    SellerList list;
    list.sequence = 7;
    list.signedAt = now;
    list.authorizationSequence = chain.authorization.sequence;
    PublishedReport report{QStringLiteral("0123456789abcdef"), QStringLiteral("scam"), QStringLiteral("Never shipped."),
                           QStringLiteral("2026-08"), true};
    list.sellers = {ListedSeller{QStringLiteral("made_up_seller"), {report}}};

    QString why;
    const std::optional<TrustedList> trusted = checkBundle(bundle(chain, list, chain.serverSign), chain.info, 6, now, &why);
    QVERIFY2(trusted, qPrintable(why));
    QCOMPARE(trusted->list.sequence, 7);
    QVERIFY(trusted->list.find(QStringLiteral("MADE_UP_SELLER")));
    QCOMPARE(trusted->list.find(QStringLiteral("made_up_seller"))->reports.first().text, QStringLiteral("Never shipped."));

    // No rollback or replay.
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), chain.info, 7, now, &why));
    // Signed by a key the master never authorized.
    const SignKeys stranger = *newSignKeys();
    QVERIFY(!checkBundle(bundle(chain, list, stranger), chain.info, 0, now, &why));
    // Another master.
    MasterInfo otherMaster{newSignKeys()->pub, kHost};
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), otherMaster, 0, now, &why));
    // Another host name than the app knows.
    MasterInfo otherHost{chain.master.pub, QStringLiteral("other.example.org")};
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), otherHost, 0, now, &why));
    // Outside the authorization's validity.
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), chain.info, 0, now.addDays(31), &why));
    // A list naming another authorization.
    SellerList wrongAuthorization = list;
    wrongAuthorization.authorizationSequence = 2;
    QVERIFY(!checkBundle(bundle(chain, wrongAuthorization, chain.serverSign), chain.info, 0, now, &why));
    // A list from the future.
    SellerList future = list;
    future.signedAt = now.addSecs(3600);
    QVERIFY(!checkBundle(bundle(chain, future, chain.serverSign), chain.info, 0, now, &why));
    // A changed body breaks the signature.
    QJsonObject tampered = bundle(chain, list, chain.serverSign);
    QJsonObject signedList = tampered.value(QStringLiteral("list")).toObject();
    QJsonObject body = signedList.value(QStringLiteral("body")).toObject();
    body.insert(QStringLiteral("sequence"), 8);
    signedList.insert(QStringLiteral("body"), body);
    tampered.insert(QStringLiteral("list"), signedList);
    QVERIFY(!checkBundle(tampered, chain.info, 0, now, &why));

    // A master-signed revocation of the server key stops the list; one signed
    // by anyone else has no effect.
    const Revocation revocation{chain.serverSign.pub, 1, now};
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign, {*signDocument(revocation.toBody(), chain.master)}), chain.info, 0,
                         now, &why));
    QVERIFY(checkBundle(bundle(chain, list, chain.serverSign, {*signDocument(revocation.toBody(), stranger)}), chain.info, 0, now,
                        &why));
    // checkKeys gives the box key through the same chain.
    const std::optional<Authorization> keys
        = checkKeys(QJsonObject{{QStringLiteral("authorization"), chain.authorizationDoc}}, chain.info, now, &why);
    QVERIFY(keys);
    QCOMPARE(keys->serverBox, chain.serverBox.pub);
}

void TestSellerList::reportChecks()
{
    const InstallKey key = *newInstallKey();
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const Report report = signedReport(QStringLiteral("made_up_seller"), QStringLiteral("123456789012"), key, now);
    QCOMPARE(report.problem(), QString());
    const std::optional<Report> parsed = Report::fromJson(report.toJson());
    QVERIFY(parsed);
    QCOMPARE(parsed->problem(), QString());

    Report changed = report;
    changed.text = QStringLiteral("Something else.");
    QVERIFY(!changed.problem().isEmpty()); // the signature covers the text
    Report noSignature = report;
    noSignature.installSignature.clear();
    QVERIFY(!noSignature.problem().isEmpty());
    Report control = report;
    control.text = QStringLiteral("bad\x01text");
    control.installSignature = installSign(control.signedBytes(), key);
    QVERIFY(!control.problem().isEmpty());
    Report tooLong = report;
    tooLong.text = QString(kMaxReportText + 1, QLatin1Char('a'));
    tooLong.installSignature = installSign(tooLong.signedBytes(), key);
    QVERIFY(!tooLong.problem().isEmpty());
    Report badItem = report;
    badItem.itemId = QStringLiteral("12ab");
    badItem.installSignature = installSign(badItem.signedBytes(), key);
    QVERIFY(!badItem.problem().isEmpty());
    Report badReason = report;
    badReason.reason = QStringLiteral("not_a_reason");
    badReason.installSignature = installSign(badReason.signedBytes(), key);
    QVERIFY(!badReason.problem().isEmpty());

    QCOMPARE((Dispute{QStringLiteral("made_up_seller"), QString(), QStringLiteral("This is wrong.")}.problem()), QString());
    const Dispute badId{QStringLiteral("made_up_seller"), QStringLiteral("XYZ"), QStringLiteral("x")};
    QVERIFY(!badId.problem().isEmpty());
    const Dispute tooLongDispute{QStringLiteral("made_up_seller"), QString(), QString(kMaxDisputeText + 1, QLatin1Char('a'))};
    QVERIFY(!tooLongDispute.problem().isEmpty());
}

void TestSellerList::httpLimits()
{
    HttpServer::Limits limits;
    limits.maxBodyBytes = 100;
    limits.maxHeaderBytes = 300;
    HttpServer server([](const HttpServer::Request &request) {
        return HttpServer::Response{200, "text/plain", request.method + ' ' + request.path + ' ' + QByteArray::number(request.body.size())};
    }, limits);
    QString error;
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, &error), qPrintable(error));
    const quint16 port = server.port();
    QByteArray answer;
    // The server runs in this thread's event loop: talk to it from another thread.
    // Each exchange ends within the client's own 5 s waits; the server's
    // event loop runs meanwhile.
    auto ask = [&](const QByteArray &request) {
        answer.clear();
        std::atomic<bool> done = false;
        std::thread worker([&] {
            answer = rawExchange(port, request);
            done = true;
        });
        while (!done)
            QTest::qWait(20);
        worker.join();
    };
    ask("GET /v1/list?x=1 HTTP/1.1\r\nHost: x\r\n\r\n");
    QVERIFY2(answer.startsWith("HTTP/1.1 200 OK\r\n"), answer.constData());
    QVERIFY(answer.endsWith("GET /v1/list 0"));
    ask("POST /v1/report HTTP/1.1\r\nContent-Length: 5\r\n\r\nhello");
    QVERIFY(answer.endsWith("POST /v1/report 5"));
    ask("POST /v1/report HTTP/1.1\r\nContent-Length: 101\r\n\r\n");
    QVERIFY2(answer.startsWith("HTTP/1.1 413"), answer.constData());
    ask("POST /v1/report HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n");
    QVERIFY2(answer.startsWith("HTTP/1.1 411"), answer.constData());
    ask("GET /v1/list HTTP/1.1\r\nX: " + QByteArray(400, 'a') + "\r\n\r\n");
    QVERIFY2(answer.startsWith("HTTP/1.1 431"), answer.constData());
    ask("NONSENSE\r\n\r\n");
    QVERIFY2(answer.startsWith("HTTP/1.1 400"), answer.constData());
}

void TestSellerList::serverHostNames()
{
    for (const char *good : {"list.example.org", "a.example.org", "seller-list.example.co.uk", "x1.example.org"})
        QVERIFY2(isServerHost(QString::fromLatin1(good)), good);
    for (const char *bad : {"", "localhost", "example", "List.example.org", "list.example.org.", "127.0.0.1",
                            "list.example.org:443", "https://list.example.org", "-a.example.org", "a-.example.org",
                            "a..example.org", "list.example.1", "list example.org", "list.example.org/v1", "a.b",
                            "list.example.org\n", "a\n.example.org"})
        QVERIFY2(!isServerHost(QString::fromLatin1(bad)), bad);
    QVERIFY(!isServerHost(QStringLiteral("a").repeated(64) + QStringLiteral(".example.org")));
    // A master.json with a host the app may not use is no master at all.
    const SignKeys master = *newSignKeys();
    QVERIFY(MasterInfo::fromJson({{QStringLiteral("master"), master.pub.toJson()}, {QStringLiteral("host"), kHost}}));
    QVERIFY(!MasterInfo::fromJson({{QStringLiteral("master"), master.pub.toJson()}, {QStringLiteral("host"), QStringLiteral("192.0.2.1")}}));
    QVERIFY(!MasterInfo::fromJson({{QStringLiteral("master"), master.pub.toJson()}}));
}

void TestSellerList::clientNotSetUp()
{
    // Without the server's host name (not set up), nothing goes out.
    QTemporaryDir dir;
    QNetworkAccessManager network;
    ListClient unset(&network, MasterInfo{newSignKeys()->pub, QString()}, dir.path());
    QSignalSpy unsetFetched(&unset, &ListClient::listFetched);
    unset.fetchList();
    QCOMPARE(unsetFetched.size(), 1);
    QCOMPARE(unsetFetched.first().at(0).toBool(), false);
    QVERIFY(unsetFetched.first().at(1).toString().contains(QStringLiteral("not set up")));
}

namespace {

// A server data set as the owner's commands would make it.
struct LocalServer {
    QTemporaryDir dir;
    Chain chain;
    ServerStore store;
    std::unique_ptr<Service> service;
    std::unique_ptr<HttpServer> http;

    bool start(const Service::Limits &limits, QString *error, const HttpServer::Tls *tls = nullptr)
    {
        chain = makeChain(QDateTime::currentDateTimeUtc());
        if (!store.open(dir.filePath(QStringLiteral("store.sqlite")), QStringLiteral("test-%1").arg(quintptr(this)), error))
            return false;
        Service::Keys keys{chain.serverSign, chain.serverBox, chain.master.pub, chain.authorizationDoc, {}};
        service = std::make_unique<Service>(&store, keys, limits, dir.filePath(QStringLiteral("list.json")));
        if (!service->start(error))
            return false;
        http = std::make_unique<HttpServer>([this](const HttpServer::Request &request) { return service->handle(request); });
        if (tls && !http->setTls(*tls, error))
            return false;
        return http->listen(QHostAddress::LocalHost, 0, error);
    }
    ~LocalServer()
    {
        http.reset();
        service.reset();
        store.close();
    }
};

} // namespace

void TestSellerList::serviceEndToEnd()
{
    LocalServer server;
    QString error;
    QVERIFY2(server.start(Service::Limits{}, &error), qPrintable(error));
    const QString item = QStringLiteral("123456789012");

    QTemporaryDir appDir;
    QNetworkAccessManager network;
    ListClient client(&network, server.chain.info, appDir.path());
    client.setLocalTestServer(server.http->port());

    // The first list is empty and verifies.
    QSignalSpy fetched(&client, &ListClient::listFetched);
    client.fetchList();
    QVERIFY(fetched.wait(10000));
    QVERIFY2(fetched.last().at(0).toBool(), qPrintable(fetched.last().at(1).toString()));
    const qint64 firstSequence = client.list()->list.sequence;
    QVERIFY(client.list()->list.sellers.isEmpty());
    // The sequence is at least the time: a server that lost its database
    // still signs lists the apps accept as newer.
    QVERIFY(firstSequence >= QDateTime::currentSecsSinceEpoch() - 60);

    // A report goes live at once, "item checked" as the reporter's app found
    // it (owner decision a, 2026-10-21: the server loads no eBay page).
    QSignalSpy settled(server.service.get(), &Service::reportSettled);
    QSignalSpy sent(&client, &ListClient::reportSent);
    Report report{QStringLiteral("made_up_seller"), QStringLiteral("not_as_described"), QStringLiteral("  Arrived broken.  "),
                  item, QStringLiteral("2026-09"), {}, {}, {}};
    report.itemCheckedInApp = true;
    client.sendReport(report);
    QVERIFY(sent.wait(10000));
    QVERIFY2(sent.last().at(0).toBool(), qPrintable(sent.last().at(2).toString()));
    QCOMPARE(sent.last().at(1).toString(), QStringLiteral("received"));
    QCOMPARE(settled.size(), 1);
    QCOMPARE(settled.last().at(1).value<ServerStore::Status>(), ServerStore::Status::Published);
    // The installation key file is owner-only (Unix permissions).
    QVERIFY(QFile::exists(appDir.filePath(QStringLiteral("install.json"))));
#ifndef Q_OS_WIN
    QCOMPARE(QFile::permissions(appDir.filePath(QStringLiteral("install.json"))) & (QFileDevice::ReadGroup | QFileDevice::ReadOther),
             QFileDevice::Permissions());
#endif

    client.fetchList();
    QVERIFY(fetched.wait(10000));
    QVERIFY2(fetched.last().at(0).toBool(), qPrintable(fetched.last().at(1).toString()));
    QVERIFY(client.list()->list.sequence > firstSequence);
    const ListedSeller *seller = client.list()->list.find(QStringLiteral("made_up_seller"));
    QVERIFY(seller);
    QCOMPARE(seller->reports.size(), 1);
    QCOMPARE(seller->reports.first().text, QStringLiteral("Arrived broken."));
    QVERIFY(seller->reports.first().itemChecked);
    QCOMPARE(seller->reports.first().month, QStringLiteral("2026-09"));
    // Nothing that identifies the reporter is published: no installation key,
    // no item number.
    QVERIFY(!server.service->bundle().contains("installKey"));
    QVERIFY(!server.service->bundle().contains(item.toLatin1()));
    QVERIFY(!server.service->bundle().contains("itemId"));

    // The saved copy loads again and holds.
    ListClient reloaded(&network, server.chain.info, appDir.path());
    QVERIFY2(reloaded.loadCache(QDateTime::currentDateTimeUtc(), &error), qPrintable(error));
    QCOMPARE(reloaded.list()->list.sequence, client.list()->list.sequence);

    // The same installation cannot report the same seller twice.
    client.sendReport(report);
    QVERIFY(sent.wait(10000));
    QCOMPARE(sent.last().at(0).toBool(), false);
    QCOMPARE(sent.last().at(1).toString(), QStringLiteral("refused"));

    // A dispute goes to the owner's queue.
    QSignalSpy disputed(&client, &ListClient::disputeSent);
    client.sendDispute(Dispute{QStringLiteral("made_up_seller"), seller->reports.first().id, QStringLiteral("This never happened.")});
    QVERIFY(disputed.wait(10000));
    QVERIFY2(disputed.last().at(0).toBool(), qPrintable(disputed.last().at(2).toString()));
    QCOMPARE(server.store.openDisputes(&error)->size(), 1);

    // The owner removes the report: the next list drops it.
    QVERIFY(server.store.setStatus(seller->reports.first().id, ServerStore::Status::Removed, QString(), &error));
    QVERIFY(server.service->publish(false, &error));
    client.fetchList();
    QVERIFY(fetched.wait(10000));
    QVERIFY(fetched.last().at(0).toBool());
    QVERIFY(!client.list()->list.find(QStringLiteral("made_up_seller")));

    // A report whose item the app could not check goes live too, without
    // "item checked".
    const QString other = QStringLiteral("223456789012");
    client.sendReport(Report{QStringLiteral("second_made_up"), QStringLiteral("scam"), QStringLiteral("No item."), other,
                             QStringLiteral("2026-09"), {}, {}, {}});
    QVERIFY(sent.wait(10000));
    QVERIFY(sent.last().at(0).toBool());
    QCOMPARE(settled.last().at(1).value<ServerStore::Status>(), ServerStore::Status::Published);
    client.fetchList();
    QVERIFY(fetched.wait(10000));
    QVERIFY(fetched.last().at(0).toBool());
    const ListedSeller *unchecked = client.list()->list.find(QStringLiteral("second_made_up"));
    QVERIFY(unchecked);
    QVERIFY(!unchecked->reports.first().itemChecked);
    // A report without the app's flag is not a report of this version.
    QJsonObject old = report.toJson();
    old.remove(QStringLiteral("itemCheckedInApp"));
    QVERIFY(!Report::fromJson(old));
    QCOMPARE(server.store.reports(ServerStore::Status::Held, &error)->size(), 0);
}

void TestSellerList::serviceLimits()
{
    LocalServer server;
    QString error;
    Service::Limits limits;
    limits.perInstallPerDay = 2;
    limits.sellerBurst = 2;
    QVERIFY2(server.start(limits, &error), qPrintable(error));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    auto post = [&](const QByteArray &body, const char *path) {
        return server.service->handle(HttpServer::Request{"POST", path, body});
    };
    auto sealed = [&](const Report &report) {
        return *seal(QJsonDocument(report.toJson()).toJson(QJsonDocument::Compact), server.chain.serverBox.pub, QStringLiteral("report"));
    };

    // Not openable, wrong label, unreadable, badly signed, too old.
    QCOMPARE(post("garbage", "/v1/report").status, 400);
    const InstallKey key = *newInstallKey();
    const Report good = signedReport(QStringLiteral("seller_one"), QStringLiteral("123456789012"), key, now);
    QCOMPARE(post(*seal(QJsonDocument(good.toJson()).toJson(), server.chain.serverBox.pub, QStringLiteral("dispute")), "/v1/report").status,
             400);
    QCOMPARE(post(*seal("not json", server.chain.serverBox.pub, QStringLiteral("report")), "/v1/report").status, 400);
    Report forged = good;
    forged.text = QStringLiteral("changed after signing");
    QCOMPARE(post(sealed(forged), "/v1/report").status, 400);
    QCOMPARE(post(sealed(signedReport(QStringLiteral("seller_one"), QStringLiteral("123456789012"), key, now.addDays(-2))),
                  "/v1/report")
                 .status,
             400);
    QCOMPARE(server.store.reportsSince(now.addDays(-1), &error).value(), 0);

    // Per installation: one report per seller, perInstallPerDay a day.
    QCOMPARE(post(sealed(good), "/v1/report").status, 202);
    QCOMPARE(post(sealed(signedReport(QStringLiteral("seller_one"), QStringLiteral("223456789012"), key, now)), "/v1/report").status, 409);
    QCOMPARE(post(sealed(signedReport(QStringLiteral("seller_two"), QStringLiteral("223456789012"), key, now)), "/v1/report").status, 202);
    QCOMPARE(post(sealed(signedReport(QStringLiteral("seller_three"), QStringLiteral("323456789012"), key, now)), "/v1/report").status, 429);

    // Per seller: from sellerBurst reports in 24 hours on, new ones are held.
    const InstallKey second = *newInstallKey();
    const InstallKey third = *newInstallKey();
    QCOMPARE(post(sealed(signedReport(QStringLiteral("seller_one"), QStringLiteral("423456789012"), second, now)), "/v1/report").status, 202);
    const HttpServer::Response held
        = post(sealed(signedReport(QStringLiteral("seller_one"), QStringLiteral("523456789012"), third, now)), "/v1/report");
    QCOMPARE(held.status, 202);
    QCOMPARE(QJsonDocument::fromJson(held.body).object().value(QStringLiteral("result")).toString(), QStringLiteral("held"));

    // Wrong methods and paths.
    QCOMPARE(server.service->handle(HttpServer::Request{"GET", "/v1/report", {}}).status, 405);
    QCOMPARE(server.service->handle(HttpServer::Request{"POST", "/v1/list", {}}).status, 405);
    QCOMPARE(server.service->handle(HttpServer::Request{"GET", "/elsewhere", {}}).status, 404);

    // Disputes: a daily cap.
    Service::Limits few;
    few.maxDisputesPerDay = 1;
    LocalServer small;
    QVERIFY2(small.start(few, &error), qPrintable(error));
    const QByteArray dispute = QJsonDocument(Dispute{QStringLiteral("seller_one"), QString(), QStringLiteral("Wrong.")}.toJson())
                                   .toJson(QJsonDocument::Compact);
    QCOMPARE(small.service->handle(HttpServer::Request{"POST", "/v1/dispute",
                                                       *seal(dispute, small.chain.serverBox.pub, QStringLiteral("dispute"))})
                 .status,
             202);
    QCOMPARE(small.service->handle(HttpServer::Request{"POST", "/v1/dispute",
                                                       *seal(dispute, small.chain.serverBox.pub, QStringLiteral("dispute"))})
                 .status,
             503);

    // A server whose authorization is for other keys does not start.
    LocalServer mismatched;
    mismatched.chain = makeChain(now);
    QVERIFY(mismatched.store.open(mismatched.dir.filePath(QStringLiteral("s.sqlite")), QStringLiteral("mismatch"), &error));
    Service::Keys keys{*newSignKeys(), mismatched.chain.serverBox, mismatched.chain.master.pub, mismatched.chain.authorizationDoc, {}};
    Service refused(&mismatched.store, keys, {}, mismatched.dir.filePath(QStringLiteral("list.json")));
    QVERIFY(!refused.start(&error));
    // Nor one whose key the master revoked.
    const Revocation revocation{mismatched.chain.serverSign.pub, 1, now};
    Service::Keys revokedKeys{mismatched.chain.serverSign, mismatched.chain.serverBox, mismatched.chain.master.pub,
                              mismatched.chain.authorizationDoc, {*signDocument(revocation.toBody(), mismatched.chain.master)}};
    Service revoked(&mismatched.store, revokedKeys, {}, mismatched.dir.filePath(QStringLiteral("list.json")));
    QVERIFY(!revoked.start(&error));
    QVERIFY(error.contains(QStringLiteral("revoked")));
}

namespace {

// Copies fixtures/tls/<name> to `path`, replacing it. A copy from the
// resources is read-only (on Windows a read-only file cannot be removed), so
// it is made writable at once. False when any step fails.
bool copyFixture(const QString &name, const QString &path)
{
    if (QFile::exists(path) && !QFile::remove(path))
        return false;
    return QFile::copy(QStringLiteral(":/tls/") + name, path)
        && QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// A test certificate or key from fixtures/tls, copied to `dir` (the server
// reads files); an empty path when the copy failed.
QString fixtureFile(const QTemporaryDir &dir, const QString &name)
{
    const QString path = dir.filePath(name);
    return copyFixture(name, path) ? path : QString();
}

QSslCertificate fixtureCertificate(const QString &name)
{
    const QList<QSslCertificate> certificates = QSslCertificate::fromPath(QStringLiteral(":/tls/") + name);
    return certificates.isEmpty() ? QSslCertificate() : certificates.first();
}

} // namespace

void TestSellerList::httpsEndToEnd()
{
    QVERIFY2(QSslSocket::supportsSsl(), qPrintable(QSslSocket::activeBackend()));
    QTemporaryDir files;
    const HttpServer::Tls tls{fixtureFile(files, QStringLiteral("cert-a.pem")), fixtureFile(files, QStringLiteral("key-a.pem"))};
    LocalServer server;
    QString error;
    QVERIFY2(server.start(Service::Limits{}, &error, &tls), qPrintable(error));

    // The server's certificate is trusted: the list comes over HTTPS and
    // verifies; a dispute goes out the same way (GET /v1/keys, then POST).
    QTemporaryDir appDir;
    QNetworkAccessManager network;
    ListClient client(&network, server.chain.info, appDir.path());
    client.setLocalTestServer(server.http->port(), fixtureCertificate(QStringLiteral("ca-a.pem")));
    QSignalSpy fetched(&client, &ListClient::listFetched);
    client.fetchList();
    QVERIFY(fetched.wait(10000));
    QVERIFY2(fetched.last().at(0).toBool(), qPrintable(fetched.last().at(1).toString()));
    QSignalSpy disputed(&client, &ListClient::disputeSent);
    client.sendDispute(Dispute{QStringLiteral("made_up_seller"), QString(), QStringLiteral("Synthetic dispute.")});
    QVERIFY(disputed.wait(10000));
    QVERIFY2(disputed.last().at(0).toBool(), qPrintable(disputed.last().at(2).toString()));
    QCOMPARE(server.store.openDisputes(&error)->size(), 1);

    // A certificate from a CA the client does not trust: nothing is fetched,
    // and no TLS error is ignored.
    // (Its own network manager: one manager reuses what it learned about a
    // host and port; the app has one manager with one trust setting.)
    QTemporaryDir otherDir;
    QNetworkAccessManager otherNetwork;
    ListClient untrusting(&otherNetwork, server.chain.info, otherDir.path());
    untrusting.setLocalTestServer(server.http->port(), fixtureCertificate(QStringLiteral("ca-b.pem")));
    QSignalSpy refused(&untrusting, &ListClient::listFetched);
    untrusting.fetchList();
    QVERIFY(refused.wait(10000));
    QCOMPARE(refused.last().at(0).toBool(), false);
    QVERIFY2(refused.last().at(1).toString().contains(QStringLiteral("could not be reached")),
             qPrintable(refused.last().at(1).toString()));
    QVERIFY(!untrusting.list());

    // Only counts are kept: the list fetch, then GET /v1/keys and the POST of
    // the dispute (one request per connection). The refusing client sent no
    // request; its connection may still be counted (on macOS it was: the
    // server had finished its side of the handshake). The server's side of
    // that connection is let settle first (the client's answer comes from
    // the network manager's own thread).
    QTRY_COMPARE_WITH_TIMEOUT(server.http->openConnections(), 0, 5000);
    QTest::qWait(200);
    const HttpServer::Counts counts = server.http->takeCounts();
    QCOMPARE(counts.replies.value(200), 2);
    QCOMPARE(counts.replies.value(202), 1);
    QCOMPARE(counts.replies.size(), 2);
    QVERIFY2(counts.connections == 3 || counts.connections == 4, qPrintable(QString::number(counts.connections)));

    // Files that hold no certificate or key are refused at the start.
    HttpServer broken([](const HttpServer::Request &) { return HttpServer::Response{}; });
    QVERIFY(!broken.setTls({tls.keyFile, tls.keyFile}, &error));
    QVERIFY(!broken.setTls({tls.certificateFile, tls.certificateFile}, &error));
    QVERIFY(!broken.setTls({files.filePath(QStringLiteral("missing.pem")), tls.keyFile}, &error));
    // A key of the right kind that is not the certificate's: the loopback
    // handshake fails.
    QVERIFY(!broken.setTls({tls.certificateFile, fixtureFile(files, QStringLiteral("key-b.pem"))}, &error));
    QVERIFY2(error.contains(QStringLiteral("handshake")), qPrintable(error));
    // Not once the server listens.
    QVERIFY(broken.listen(QHostAddress::LocalHost, 0, &error));
    QVERIFY(!broken.setTls(tls, &error));
}

void TestSellerList::tlsRenewal()
{
    QTemporaryDir files;
    const HttpServer::Tls tls{fixtureFile(files, QStringLiteral("cert-a.pem")), fixtureFile(files, QStringLiteral("key-a.pem"))};
    HttpServer::Limits limits;
    limits.tlsCheckMs = 50;
    HttpServer server([](const HttpServer::Request &) { return HttpServer::Response{200, "application/json", "{}"}; }, limits);
    QString error;
    QVERIFY2(server.setTls(tls, &error), qPrintable(error));
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0, &error), qPrintable(error));

    QNetworkAccessManager network;
    // GET / against the server, trusting only `root`: true when it answers.
    auto answers = [&](const QSslCertificate &root) {
        QNetworkRequest request(QUrl(QStringLiteral("https://127.0.0.1:%1/").arg(server.port())));
        QSslConfiguration configuration = request.sslConfiguration();
        configuration.setCaCertificates({root});
        request.setSslConfiguration(configuration);
        network.clearConnectionCache();
        QNetworkReply *reply = network.get(request);
        QSignalSpy finished(reply, &QNetworkReply::finished);
        const bool done = finished.wait(10000);
        const bool ok = done && reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        return ok;
    };
    const QSslCertificate a = fixtureCertificate(QStringLiteral("ca-a.pem"));
    const QSslCertificate b = fixtureCertificate(QStringLiteral("ca-b.pem"));
    QVERIFY(answers(a));
    QVERIFY(!answers(b));

    // A renewal replaces both files: the new pair is used without a restart.
    QSignalSpy reloaded(&server, &HttpServer::tlsReloaded);
    QSignalSpy failed(&server, &HttpServer::tlsReloadFailed);
    // 2.1 s: a newer file time than the first pair's, also on file systems
    // that keep times in 2 s steps (FAT).
    QTest::qWait(2100);
    QVERIFY(copyFixture(QStringLiteral("cert-b.pem"), tls.certificateFile));
    QVERIFY(copyFixture(QStringLiteral("key-b.pem"), tls.keyFile));
    QVERIFY(reloaded.wait(5000));
    QVERIFY(answers(b));
    QVERIFY(!answers(a));

    // A renewed pair whose key is not the certificate's is reported once,
    // and the last good pair stays in use.
    QTest::qWait(2100);
    QVERIFY(copyFixture(QStringLiteral("cert-a.pem"), tls.certificateFile));
    QVERIFY(failed.wait(5000));
    QTest::qWait(300); // several more checks: no second report for the same pair
    QCOMPARE(failed.size(), 1);
    QVERIFY(answers(b));

    // A renewed pair that does not load is reported; the last good one stays.
    QTest::qWait(2100);
    QVERIFY(QFile::remove(tls.certificateFile));
    QFile garbage(tls.certificateFile);
    QVERIFY(garbage.open(QIODevice::WriteOnly));
    garbage.write("not a certificate\n");
    garbage.close();
    QVERIFY(failed.wait(5000));
    QCOMPARE(failed.size(), 2);
    QVERIFY(answers(b));
}

QTEST_GUILESS_MAIN(TestSellerList)
#include "tst_sellerlist.moc"
