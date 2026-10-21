// The shared seller list: hybrid cryptography, the trust chain, reports, the
// server's checks and limits, and the app's client against a local server.
// Everything is synthetic: made-up seller ids, item numbers and onion names.

#include "HttpServer.h"
#include "ItemCheck.h"
#include "KeyFiles.h"
#include "ServerStore.h"
#include "Service.h"
#include "sellerlist/Crypto.h"
#include "sellerlist/ListClient.h"
#include "sellerlist/Protocol.h"
#include "sellerlist/TorRunner.h"
#include "sellerlist/Trust.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>

using namespace app::sellerlist;
using sellerserver::HttpServer;
using sellerserver::ItemVerdict;
using sellerserver::ServerStore;
using sellerserver::Service;

namespace {

const QString kOnion = QStringLiteral("abcdefghijklmnopqrstuvwxyz234567abcdefghijklmnopqrstuvwx.onion");

// A synthetic item page with what the server reads: the JSON-LD ItemPage and
// the seller's feedback links of the item.
QByteArray itemPage(const QString &itemId, const QStringList &sellers, bool jsonLd = true)
{
    QByteArray html = "<html><head>";
    if (jsonLd)
        html += R"(<script type="application/ld+json">{"@context":"https://schema.org","@type":"ItemPage","name":"x"}</script>)";
    html += "</head><body>";
    for (const QString &seller : sellers) {
        html += "<a href=\"https://www.example.test/fdbk/mweb_profile?fdbkType=FeedbackReceivedAsSeller&amp;item_id="
            + itemId.toLatin1() + "&amp;username=" + seller.toLatin1() + "&amp;sort=RELEVANCE\">ratings</a>";
    }
    html += "</body></html>";
    return html;
}

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
    chain.authorization = {chain.serverSign.pub, chain.serverBox.pub, kOnion, sequence, now.addDays(-1), now.addDays(30)};
    chain.authorizationDoc = *signDocument(chain.authorization.toBody(), chain.master);
    chain.info = {chain.master.pub, kOnion};
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
    void itemPageReader();
    void httpLimits();
    void clientRefusesDirect();
    void serviceEndToEnd();
    void serviceLimits();
    void torRunnerControlProtocol();
    void torRunnerMissingProgram();
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
    MasterInfo otherMaster{newSignKeys()->pub, kOnion};
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), otherMaster, 0, now, &why));
    // Another onion address than the app knows.
    MasterInfo otherOnion{chain.master.pub, QStringLiteral("b").repeated(56) + QStringLiteral(".onion")};
    QVERIFY(!checkBundle(bundle(chain, list, chain.serverSign), otherOnion, 0, now, &why));
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

void TestSellerList::itemPageReader()
{
    const QString item = QStringLiteral("123456789012");
    QCOMPARE(sellerserver::readItemPage(itemPage(item, {QStringLiteral("Made_Up_Seller")}), item, QStringLiteral("made_up_seller")).verdict,
             ItemVerdict::Confirmed);
    QCOMPARE(sellerserver::readItemPage(itemPage(item, {QStringLiteral("someone_else")}), item, QStringLiteral("made_up_seller")).verdict,
             ItemVerdict::OtherSeller);
    QCOMPARE(sellerserver::readItemPage(itemPage(item, {QStringLiteral("made_up_seller")}, false), item, QStringLiteral("made_up_seller")).verdict,
             ItemVerdict::NotReadable);
    QCOMPARE(sellerserver::readItemPage(itemPage(item, {}), item, QStringLiteral("made_up_seller")).verdict, ItemVerdict::NotReadable);
    QCOMPARE(sellerserver::readItemPage(itemPage(item, {QStringLiteral("made_up_seller"), QStringLiteral("someone_else")}), item,
                                        QStringLiteral("made_up_seller"))
                 .verdict,
             ItemVerdict::NotReadable);
    // Links of another item do not count.
    QCOMPARE(sellerserver::readItemPage(itemPage(QStringLiteral("999999999999"), {QStringLiteral("made_up_seller")}), item,
                                        QStringLiteral("made_up_seller"))
                 .verdict,
             ItemVerdict::NotReadable);
    QCOMPARE(sellerserver::readItemPage("<html>a sign-in page</html>", item, QStringLiteral("made_up_seller")).verdict,
             ItemVerdict::NotReadable);
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

void TestSellerList::clientRefusesDirect()
{
    QTemporaryDir dir;
    QNetworkAccessManager network;
    network.setProxy(QNetworkProxy::NoProxy);
    ListClient client(&network, MasterInfo{newSignKeys()->pub, kOnion}, dir.path());
    QSignalSpy fetched(&client, &ListClient::listFetched);
    client.fetchList();
    QCOMPARE(fetched.size(), 1);
    QCOMPARE(fetched.first().at(0).toBool(), false);
    QVERIFY(fetched.first().at(1).toString().contains(QStringLiteral("never connects directly")));

    // A proxy that is not a local SOCKS5 proxy is refused too.
    network.setProxy(QNetworkProxy(QNetworkProxy::HttpProxy, QStringLiteral("127.0.0.1"), 8080));
    client.fetchList();
    QCOMPARE(fetched.size(), 2);
    QCOMPARE(fetched.last().at(0).toBool(), false);

    // Without the onion address (not set up), nothing goes out either.
    ListClient unset(&network, MasterInfo{newSignKeys()->pub, QString()}, dir.path());
    QSignalSpy unsetFetched(&unset, &ListClient::listFetched);
    unset.fetchList();
    QCOMPARE(unsetFetched.size(), 1);
    QCOMPARE(unsetFetched.first().at(0).toBool(), false);
}

namespace {

// A server data set as the owner's commands would make it, plus a fake item
// page site.
struct LocalServer {
    QTemporaryDir dir;
    Chain chain;
    ServerStore store;
    std::unique_ptr<Service> service;
    std::unique_ptr<HttpServer> http;
    std::unique_ptr<HttpServer> pages;
    QHash<QString, QByteArray> pageByItem;

    bool start(const Service::Limits &limits, QString *error)
    {
        chain = makeChain(QDateTime::currentDateTimeUtc());
        pages = std::make_unique<HttpServer>([this](const HttpServer::Request &request) {
            const QString item = QString::fromLatin1(request.path.mid(5));
            if (!request.path.startsWith("/itm/") || !pageByItem.contains(item))
                return HttpServer::Response{404, "text/plain", "no"};
            return HttpServer::Response{200, "text/html", pageByItem.value(item)};
        });
        if (!pages->listen(QHostAddress::LocalHost, 0, error))
            return false;
        if (!store.open(dir.filePath(QStringLiteral("store.sqlite")), QStringLiteral("test-%1").arg(quintptr(this)), error))
            return false;
        sellerserver::ItemPageFetcher::Settings fetcher;
        fetcher.base = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(pages->port()));
        fetcher.gapMs = 10;
        Service::Keys keys{chain.serverSign, chain.serverBox, chain.master.pub, chain.authorizationDoc, {}};
        service = std::make_unique<Service>(&store, keys, fetcher, limits, dir.filePath(QStringLiteral("list.json")));
        if (!service->start(error))
            return false;
        http = std::make_unique<HttpServer>([this](const HttpServer::Request &request) { return service->handle(request); });
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
    server.pageByItem.insert(item, itemPage(item, {QStringLiteral("made_up_seller")}));

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

    // A report goes live after the item check.
    QSignalSpy settled(server.service.get(), &Service::reportSettled);
    QSignalSpy sent(&client, &ListClient::reportSent);
    Report report{QStringLiteral("made_up_seller"), QStringLiteral("not_as_described"), QStringLiteral("  Arrived broken.  "),
                  item, QStringLiteral("2026-09"), {}, {}, {}};
    client.sendReport(report);
    QVERIFY(sent.wait(10000));
    QVERIFY2(sent.last().at(0).toBool(), qPrintable(sent.last().at(2).toString()));
    QCOMPARE(sent.last().at(1).toString(), QStringLiteral("received"));
    QVERIFY(settled.wait(10000));
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

    // An item page naming another seller holds the report.
    const QString other = QStringLiteral("223456789012");
    server.pageByItem.insert(other, itemPage(other, {QStringLiteral("someone_else")}));
    client.sendReport(Report{QStringLiteral("second_made_up"), QStringLiteral("scam"), QStringLiteral("No item."), other,
                             QStringLiteral("2026-09"), {}, {}, {}});
    QVERIFY(sent.wait(10000));
    QVERIFY(sent.last().at(0).toBool());
    QVERIFY(settled.wait(10000));
    QCOMPARE(settled.last().at(1).value<ServerStore::Status>(), ServerStore::Status::Held);
    // A missing page holds it too.
    client.sendReport(Report{QStringLiteral("third_made_up"), QStringLiteral("scam"), QStringLiteral("No item."),
                             QStringLiteral("323456789012"), QStringLiteral("2026-09"), {}, {}, {}});
    QVERIFY(sent.wait(10000));
    QVERIFY(settled.wait(10000));
    QCOMPARE(settled.last().at(1).value<ServerStore::Status>(), ServerStore::Status::Held);
    QCOMPARE(server.store.reports(ServerStore::Status::Held, &error)->size(), 2);
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
    Service refused(&mismatched.store, keys, {}, {}, mismatched.dir.filePath(QStringLiteral("list.json")));
    QVERIFY(!refused.start(&error));
    // Nor one whose key the master revoked.
    const Revocation revocation{mismatched.chain.serverSign.pub, 1, now};
    Service::Keys revokedKeys{mismatched.chain.serverSign, mismatched.chain.serverBox, mismatched.chain.master.pub,
                              mismatched.chain.authorizationDoc, {*signDocument(revocation.toBody(), mismatched.chain.master)}};
    Service revoked(&mismatched.store, revokedKeys, {}, {}, mismatched.dir.filePath(QStringLiteral("list.json")));
    QVERIFY(!revoked.start(&error));
    QVERIFY(error.contains(QStringLiteral("revoked")));
}

void TestSellerList::torRunnerControlProtocol()
{
    // FAKE_TOR_PATH: a helper that writes the control-port file and the
    // cookie, and answers the control commands as the control-spec describes.
    QTemporaryDir dir;
    TorRunner runner(QStringLiteral(FAKE_TOR_PATH), dir.filePath(QStringLiteral("tor")));
    QSignalSpy changed(&runner, &TorRunner::stateChanged);
    runner.start();
    QTRY_VERIFY_WITH_TIMEOUT(runner.state() == TorRunner::State::Ready || runner.state() == TorRunner::State::Failed, 30000);
    QVERIFY2(runner.state() == TorRunner::State::Ready, qPrintable(runner.error()));
    QCOMPARE(runner.proxy().type(), QNetworkProxy::Socks5Proxy);
    QCOMPARE(runner.proxy().hostName(), QStringLiteral("127.0.0.1"));
    QCOMPARE(runner.proxy().port(), quint16(19050));
    runner.stop();
    QCOMPARE(runner.state(), TorRunner::State::Off);

    // A tor that answers the challenge with a wrong server hash is refused
    // before the cookie's proof is sent.
    TorRunner liar(QStringLiteral(FAKE_TOR_PATH), dir.filePath(QStringLiteral("tor-liar")));
    QFile marker(dir.filePath(QStringLiteral("tor-liar/wrong-hash")));
    QVERIFY(QDir().mkpath(dir.filePath(QStringLiteral("tor-liar"))));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.close();
    liar.start();
    QTRY_VERIFY_WITH_TIMEOUT(liar.state() == TorRunner::State::Failed, 30000);
    QVERIFY2(liar.error().contains(QStringLiteral("does not match")), qPrintable(liar.error()));
}

void TestSellerList::torRunnerMissingProgram()
{
    QTemporaryDir dir;
    TorRunner runner(QString(), dir.path());
    runner.start();
    QCOMPARE(runner.state(), TorRunner::State::Failed);
    QVERIFY(runner.error().contains(QStringLiteral("not part of this build")));
}

QTEST_GUILESS_MAIN(TestSellerList)
#include "tst_sellerlist.moc"
