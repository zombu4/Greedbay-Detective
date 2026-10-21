// seller-list-server: the shared seller list's server and the owner's key
// and moderation commands (DESIGN 3.7; setup steps in README.md here).
//
// Files of a server data folder (<data>):
//   keys/server-sign.secret.json  server signing keys, encrypted (passphrase)
//   keys/server-box.secret.json   server report keys, encrypted (passphrase)
//   server.public.json            {"sign", "box"}: give to the master owner
//   master.public.json            {"master"}: the master's public keys
//   authorization.json            master-signed authorization of the server keys
//   revocations.json              {"revocations": [...]} master-signed (may be absent)
//   store.sqlite                  reports and disputes
//   list.json                     the latest signed bundle served at /v1/list
// The master folder (<master>, on the owner's own computer only):
//   master.secret.json            master signing keys, encrypted (passphrase)
//   master.public.json            {"master"}

#include "HttpServer.h"
#include "KeyFiles.h"
#include "Service.h"
#include "ServerStore.h"
#include "sellerlist/Protocol.h"

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <cstdio>

namespace sl = app::sellerlist;
using namespace sellerserver;

namespace {

const char kUsage[] = R"(usage: seller-list-server <command> [options]

On the owner's computer (master key; never on the server):
  master-keys --master <dir>
      new master keys in <dir> (asks for a passphrase)
  authorize --master <dir> --server-public <file> --host <name>
            --sequence <n> --days <n> --out <file>
      sign an authorization of the server keys for <n> days
  revoke --master <dir> --server-public <file> --sequence <n> --revocations <file>
      add a revocation of those server keys to the revocations file
  app-master --master <dir> --host <name> --out <file>
      the app's master.json (master public keys + the server's host name)

On the server:
  server-keys --data <dir>
      new server keys in <dir> (asks for a passphrase)
  serve --data <dir> --port <n> [--tls-cert <file> --tls-key <file>]
        [--listen <address>] [--passphrase-file <file>] [--test-item-pages <url>]
      run the service. With --tls-cert (fullchain.pem) and --tls-key
      (privkey.pem) it serves HTTPS on <address>:<n>, by default every
      address (port 443 for the apps); renewed files are used without a
      restart. Without them it serves plain HTTP on 127.0.0.1:<n> or
      [::1]:<n> only, for a reverse proxy on this machine that does the HTTPS.
      No client address, header or path is ever logged: once an hour one
      line with counts only.
      --test-item-pages http://127.0.0.1:<port> loads item pages from a local
      test server instead of eBay (a dry run only)
  queue --data <dir>            reports pending or held, open disputes
  publish --data <dir> --id <report>   publish a held report
  hold --data <dir> --id <report>      take a report off the list (held)
  remove --data <dir> --id <report>    remove a report for good
  close-dispute --data <dir> --id <n>  close a dispute

A passphrase is typed at the terminal (no echo) unless --passphrase-file
names a file only its owner can read. Passphrases are never taken from the
command line.
)";

const QString kHostRule = QStringLiteral("--host must be the server's DNS name in lower case, e.g. list.example.org "
                                         "(no IP address, no port, no trailing dot)");

// Each line at once, also when the output goes to a file or a service log.
void say(const QString &text)
{
    std::fputs(text.toLocal8Bit().constData(), stdout);
    std::fputs("\n", stdout);
    std::fflush(stdout);
}

int fail(const QString &text)
{
    std::fputs(text.toLocal8Bit().constData(), stderr);
    std::fputs("\n", stderr);
    std::fflush(stderr);
    return 1;
}

// --name value pairs after the command.
std::optional<QMap<QString, QString>> options(const QStringList &args, const QStringList &allowed, QString *error)
{
    QMap<QString, QString> map;
    for (qsizetype i = 2; i < args.size(); i += 2) {
        const QString name = args.at(i);
        if (!name.startsWith(QLatin1String("--")) || !allowed.contains(name.mid(2)) || i + 1 >= args.size()) {
            *error = QStringLiteral("unexpected argument %1").arg(name);
            return std::nullopt;
        }
        map.insert(name.mid(2), args.at(i + 1));
    }
    return map;
}

bool need(const QMap<QString, QString> &map, const QStringList &names, QString *error)
{
    for (const QString &name : names) {
        if (map.value(name).isEmpty()) {
            *error = QStringLiteral("--%1 is required").arg(name);
            return false;
        }
    }
    return true;
}

std::optional<qint64> number(const QString &text, qint64 min, qint64 max, const QString &name, QString *error)
{
    bool ok = false;
    const qint64 value = text.toLongLong(&ok);
    if (!ok || value < min || value > max) {
        *error = QStringLiteral("--%1 must be a whole number from %2 to %3").arg(name).arg(min).arg(max);
        return std::nullopt;
    }
    return value;
}

QByteArray pretty(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Indented); }

std::optional<sl::SignPublic> masterPublic(const QString &dir, QString *error)
{
    const std::optional<QJsonObject> file = readJsonFile(QDir(dir).filePath(QStringLiteral("master.public.json")), error);
    if (!file)
        return std::nullopt;
    std::optional<sl::SignPublic> master = sl::SignPublic::fromJson(file->value(QStringLiteral("master")).toObject());
    if (!master)
        *error = QStringLiteral("master.public.json is not readable");
    return master;
}

std::optional<sl::SignKeys> masterKeys(const QString &dir, QString *error)
{
    const std::optional<QByteArray> passphrase = readPassphrase(QString(), false, error);
    if (!passphrase)
        return std::nullopt;
    QByteArray secret = *passphrase;
    std::optional<sl::SignKeys> keys = loadSignKeys(QDir(dir).filePath(QStringLiteral("master.secret.json")), secret, error);
    sl::wipe(&secret);
    return keys;
}

struct ServerPublic {
    sl::SignPublic sign;
    sl::BoxPublic box;
};

std::optional<ServerPublic> serverPublic(const QString &path, QString *error)
{
    const std::optional<QJsonObject> file = readJsonFile(path, error);
    if (!file)
        return std::nullopt;
    const std::optional<sl::SignPublic> sign = sl::SignPublic::fromJson(file->value(QStringLiteral("sign")).toObject());
    const std::optional<sl::BoxPublic> box = sl::BoxPublic::fromJson(file->value(QStringLiteral("box")).toObject());
    if (!sign || !box) {
        *error = QStringLiteral("%1 is not a server.public.json").arg(path);
        return std::nullopt;
    }
    return ServerPublic{*sign, *box};
}

int masterKeysCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("master")}, &error))
        return fail(error);
    const QDir dir(map.value(QStringLiteral("master")));
    if (!dir.exists() && !QDir().mkpath(dir.path()))
        return fail(QStringLiteral("cannot create %1").arg(dir.path()));
    std::optional<QByteArray> passphrase = readPassphrase(QString(), true, &error);
    if (!passphrase)
        return fail(error);
    std::optional<sl::SignKeys> keys = sl::newSignKeys();
    if (!keys)
        return fail(QStringLiteral("making the keys failed"));
    const bool saved = saveSignKeys(dir.filePath(QStringLiteral("master.secret.json")), *keys, *passphrase, &error)
        && writeNewFile(dir.filePath(QStringLiteral("master.public.json")),
                        pretty(QJsonObject{{QStringLiteral("master"), keys->pub.toJson()}}), false, &error);
    const QString id = sl::fingerprint(keys->pub.ed25519 + keys->pub.mldsa65);
    wipe(&*keys);
    sl::wipe(&*passphrase);
    if (!saved)
        return fail(error);
    say(QStringLiteral("master keys written; fingerprint %1").arg(id));
    return 0;
}

int serverKeysCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("data")}, &error))
        return fail(error);
    const QDir dir(map.value(QStringLiteral("data")));
    if (!QDir().mkpath(dir.filePath(QStringLiteral("keys"))))
        return fail(QStringLiteral("cannot create %1").arg(dir.filePath(QStringLiteral("keys"))));
    QFile::setPermissions(dir.filePath(QStringLiteral("keys")), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                                    | QFileDevice::ExeOwner);
    std::optional<QByteArray> passphrase = readPassphrase(map.value(QStringLiteral("passphrase-file")), true, &error);
    if (!passphrase)
        return fail(error);
    std::optional<sl::SignKeys> sign = sl::newSignKeys();
    std::optional<sl::BoxKeys> box = sl::newBoxKeys();
    if (!sign || !box)
        return fail(QStringLiteral("making the keys failed"));
    const bool saved = saveSignKeys(dir.filePath(QStringLiteral("keys/server-sign.secret.json")), *sign, *passphrase, &error)
        && saveBoxKeys(dir.filePath(QStringLiteral("keys/server-box.secret.json")), *box, *passphrase, &error)
        && writeNewFile(dir.filePath(QStringLiteral("server.public.json")),
                        pretty(QJsonObject{{QStringLiteral("sign"), sign->pub.toJson()}, {QStringLiteral("box"), box->pub.toJson()}}),
                        false, &error);
    wipe(&*sign);
    wipe(&*box);
    sl::wipe(&*passphrase);
    if (!saved)
        return fail(error);
    say(QStringLiteral("server keys written; send server.public.json to the master owner"));
    return 0;
}

int authorizeCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("master"), QStringLiteral("server-public"), QStringLiteral("host"), QStringLiteral("sequence"),
                    QStringLiteral("days"), QStringLiteral("out")},
              &error))
        return fail(error);
    const QString host = map.value(QStringLiteral("host"));
    if (!sl::isServerHost(host))
        return fail(kHostRule);
    const std::optional<qint64> sequence = number(map.value(QStringLiteral("sequence")), 1, 1'000'000'000, QStringLiteral("sequence"), &error);
    const std::optional<qint64> days = sequence ? number(map.value(QStringLiteral("days")), 1, 3650, QStringLiteral("days"), &error)
                                                : std::nullopt;
    if (!sequence || !days)
        return fail(error);
    const std::optional<ServerPublic> server = serverPublic(map.value(QStringLiteral("server-public")), &error);
    if (!server)
        return fail(error);
    std::optional<sl::SignKeys> master = masterKeys(map.value(QStringLiteral("master")), &error);
    if (!master)
        return fail(error);
    const QDateTime from = QDateTime::currentDateTimeUtc();
    const sl::Authorization authorization{server->sign, server->box, host, *sequence, from, from.addDays(*days)};
    const std::optional<QJsonObject> document = sl::signDocument(authorization.toBody(), *master);
    wipe(&*master);
    if (!document)
        return fail(QStringLiteral("signing failed"));
    if (!writeNewFile(map.value(QStringLiteral("out")), pretty(*document), false, &error))
        return fail(error);
    say(QStringLiteral("authorization %1 written, valid until %2").arg(*sequence).arg(authorization.validUntil.toString(Qt::ISODate)));
    return 0;
}

int revokeCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("master"), QStringLiteral("server-public"), QStringLiteral("sequence"), QStringLiteral("revocations")},
              &error))
        return fail(error);
    const std::optional<qint64> sequence = number(map.value(QStringLiteral("sequence")), 1, 1'000'000'000, QStringLiteral("sequence"), &error);
    if (!sequence)
        return fail(error);
    const std::optional<ServerPublic> server = serverPublic(map.value(QStringLiteral("server-public")), &error);
    if (!server)
        return fail(error);
    const QString path = map.value(QStringLiteral("revocations"));
    QJsonArray revocations;
    if (QFile::exists(path)) {
        const std::optional<QJsonObject> file = readJsonFile(path, &error);
        if (!file)
            return fail(error);
        revocations = file->value(QStringLiteral("revocations")).toArray();
    }
    std::optional<sl::SignKeys> master = masterKeys(map.value(QStringLiteral("master")), &error);
    if (!master)
        return fail(error);
    const sl::Revocation revocation{server->sign, *sequence, QDateTime::currentDateTimeUtc()};
    const std::optional<QJsonObject> document = sl::signDocument(revocation.toBody(), *master);
    wipe(&*master);
    if (!document)
        return fail(QStringLiteral("signing failed"));
    revocations.append(*document);
    if (!replaceFile(path, pretty(QJsonObject{{QStringLiteral("revocations"), revocations}}), &error))
        return fail(error);
    say(QStringLiteral("revocation added; copy %1 to the server's data folder and restart it").arg(path));
    return 0;
}

int appMasterCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("master"), QStringLiteral("host"), QStringLiteral("out")}, &error))
        return fail(error);
    const QString host = map.value(QStringLiteral("host"));
    if (!sl::isServerHost(host))
        return fail(kHostRule);
    const std::optional<sl::SignPublic> master = masterPublic(map.value(QStringLiteral("master")), &error);
    if (!master)
        return fail(error);
    if (!writeNewFile(map.value(QStringLiteral("out")),
                      pretty(QJsonObject{{QStringLiteral("master"), master->toJson()}, {QStringLiteral("host"), host}}), false,
                      &error))
        return fail(error);
    say(QStringLiteral("written; put it in the app's source as src/sellerlist/data/master.json"));
    return 0;
}

QString countsLine(const HttpServer::Counts &counts)
{
    QStringList replies;
    for (auto it = counts.replies.cbegin(); it != counts.replies.cend(); ++it)
        replies.append(QStringLiteral("%1 x%2").arg(it.key()).arg(it.value()));
    return QStringLiteral("last hour: %1 connections, %2 turned away (all slots in use), replies: %3")
        .arg(counts.connections)
        .arg(counts.turnedAway)
        .arg(replies.isEmpty() ? QStringLiteral("none") : replies.join(QStringLiteral(", ")));
}

bool openStore(const QString &dataDir, ServerStore *store, QString *error)
{
    return store->open(QDir(dataDir).filePath(QStringLiteral("store.sqlite")), QStringLiteral("server"), error);
}

int serveCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("data"), QStringLiteral("port")}, &error))
        return fail(error);
    const std::optional<qint64> port = number(map.value(QStringLiteral("port")), 1, 65535, QStringLiteral("port"), &error);
    if (!port)
        return fail(error);
    const bool tls = !map.value(QStringLiteral("tls-cert")).isEmpty() || !map.value(QStringLiteral("tls-key")).isEmpty();
    if (tls && !need(map, {QStringLiteral("tls-cert"), QStringLiteral("tls-key")}, &error))
        return fail(error);
    QHostAddress address = tls ? QHostAddress(QHostAddress::Any) : QHostAddress(QHostAddress::LocalHost);
    if (!map.value(QStringLiteral("listen")).isEmpty() && !address.setAddress(map.value(QStringLiteral("listen"))))
        return fail(QStringLiteral("--listen must be an IP address"));
    if (!tls && !address.isLoopback())
        return fail(QStringLiteral("plain HTTP only on 127.0.0.1 or ::1 (for a reverse proxy on this machine); "
                                   "give --tls-cert and --tls-key to serve HTTPS"));
    HttpServer http([](const HttpServer::Request &) { return HttpServer::Response{503, "text/plain", "starting\n"}; });
    if (tls && !http.setTls({map.value(QStringLiteral("tls-cert")), map.value(QStringLiteral("tls-key"))}, &error))
        return fail(error);
    const QDir dir(map.value(QStringLiteral("data")));

    Service::Keys keys;
    const std::optional<sl::SignPublic> master = masterPublic(dir.path(), &error);
    const std::optional<QJsonObject> authorization = master ? readJsonFile(dir.filePath(QStringLiteral("authorization.json")), &error)
                                                            : std::nullopt;
    if (!authorization)
        return fail(error);
    keys.master = *master;
    keys.authorization = *authorization;
    if (QFile::exists(dir.filePath(QStringLiteral("revocations.json")))) {
        const std::optional<QJsonObject> revocations = readJsonFile(dir.filePath(QStringLiteral("revocations.json")), &error);
        if (!revocations)
            return fail(error);
        keys.revocations = revocations->value(QStringLiteral("revocations")).toArray();
    }
    {
        std::optional<QByteArray> passphrase = readPassphrase(map.value(QStringLiteral("passphrase-file")), false, &error);
        if (!passphrase)
            return fail(error);
        std::optional<sl::SignKeys> sign = loadSignKeys(dir.filePath(QStringLiteral("keys/server-sign.secret.json")), *passphrase, &error);
        std::optional<sl::BoxKeys> box = sign ? loadBoxKeys(dir.filePath(QStringLiteral("keys/server-box.secret.json")), *passphrase, &error)
                                              : std::nullopt;
        sl::wipe(&*passphrase);
        if (!sign || !box)
            return fail(error);
        keys.sign = *sign;
        keys.box = *box;
        wipe(&*sign);
        wipe(&*box);
    }

    ServerStore store;
    if (!openStore(dir.path(), &store, &error))
        return fail(error);
    ItemPageFetcher::Settings fetcher;
    if (!map.value(QStringLiteral("test-item-pages")).isEmpty()) {
        const QUrl base(map.value(QStringLiteral("test-item-pages")), QUrl::StrictMode);
        if (!base.isValid() || base.scheme() != QLatin1String("http") || base.host() != QLatin1String("127.0.0.1")
            || base.port() <= 0 || !base.path().isEmpty())
            return fail(QStringLiteral("--test-item-pages must be http://127.0.0.1:<port>"));
        fetcher.base = base;
        say(QStringLiteral("dry run: item pages come from %1, not from eBay").arg(base.toString()));
    }
    Service service(&store, keys, fetcher, Service::Limits{}, dir.filePath(QStringLiteral("list.json")));
    wipe(&keys.sign);
    wipe(&keys.box);
    QObject::connect(&service, &Service::problem, [](const QString &message) { fail(QStringLiteral("problem: %1").arg(message)); });
    QObject::connect(&service, &Service::published, [](qint64 sequence) { say(QStringLiteral("list %1 published").arg(sequence)); });
    if (!service.start(&error))
        return fail(error);
    http.setHandler([&service](const HttpServer::Request &request) { return service.handle(request); });
    QObject::connect(&http, &HttpServer::tlsReloaded, [] { say(QStringLiteral("renewed TLS certificate in use")); });
    QObject::connect(&http, &HttpServer::tlsReloadFailed, [](const QString &why) {
        fail(QStringLiteral("problem: the renewed TLS files do not load (the previous ones stay in use): %1").arg(why));
    });
    if (!http.listen(address, static_cast<quint16>(*port), &error))
        return fail(error);
    say(QStringLiteral("serving %1 on %2 port %3").arg(tls ? QStringLiteral("HTTPS") : QStringLiteral("HTTP"), address.toString()).arg(*port));
    // The only record of the traffic: counts, once an hour.
    QTimer counts;
    QObject::connect(&counts, &QTimer::timeout, [&http] { say(countsLine(http.takeCounts())); });
    counts.start(60 * 60 * 1000);
    const int code = QCoreApplication::exec();
    store.close();
    return code;
}

int queueCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("data")}, &error))
        return fail(error);
    ServerStore store;
    if (!openStore(map.value(QStringLiteral("data")), &store, &error))
        return fail(error);
    for (const ServerStore::Status status : {ServerStore::Status::Pending, ServerStore::Status::Held}) {
        const std::optional<QList<ServerStore::StoredReport>> reports = store.reports(status, &error);
        if (!reports)
            return fail(error);
        for (const ServerStore::StoredReport &stored : *reports) {
            say(QStringLiteral("%1 %2 seller %3 item %4 month %5 reason %6 received %7%8")
                    .arg(ServerStore::statusId(status), stored.report.id, stored.sellerId, stored.itemId, stored.report.month,
                         stored.report.reason, stored.receivedAt.toString(Qt::ISODate),
                         stored.holdReason.isEmpty() ? QString() : QStringLiteral(" - ") + stored.holdReason));
            say(QStringLiteral("    ") + stored.report.text);
        }
    }
    const std::optional<QList<ServerStore::StoredDispute>> disputes = store.openDisputes(&error);
    if (!disputes)
        return fail(error);
    for (const ServerStore::StoredDispute &stored : *disputes) {
        say(QStringLiteral("dispute %1 seller %2 report %3 received %4")
                .arg(stored.id)
                .arg(stored.dispute.sellerId, stored.dispute.reportId.isEmpty() ? QStringLiteral("-") : stored.dispute.reportId,
                     stored.receivedAt.toString(Qt::ISODate)));
        say(QStringLiteral("    ") + stored.dispute.text);
    }
    store.close();
    return 0;
}

int statusCommand(const QMap<QString, QString> &map, ServerStore::Status status)
{
    QString error;
    if (!need(map, {QStringLiteral("data"), QStringLiteral("id")}, &error))
        return fail(error);
    ServerStore store;
    if (!openStore(map.value(QStringLiteral("data")), &store, &error))
        return fail(error);
    const QString reason = status == ServerStore::Status::Held ? QStringLiteral("taken off the list by the owner") : QString();
    const bool ok = store.setStatus(map.value(QStringLiteral("id")), status, reason, &error);
    store.close();
    if (!ok)
        return fail(error);
    say(QStringLiteral("report %1 now %2; the running server signs a new list within a minute")
            .arg(map.value(QStringLiteral("id")), ServerStore::statusId(status)));
    return 0;
}

int closeDisputeCommand(const QMap<QString, QString> &map)
{
    QString error;
    if (!need(map, {QStringLiteral("data"), QStringLiteral("id")}, &error))
        return fail(error);
    const std::optional<qint64> id = number(map.value(QStringLiteral("id")), 1, std::numeric_limits<qint64>::max(), QStringLiteral("id"), &error);
    if (!id)
        return fail(error);
    ServerStore store;
    if (!openStore(map.value(QStringLiteral("data")), &store, &error))
        return fail(error);
    const bool ok = store.closeDispute(*id, &error);
    store.close();
    return ok ? 0 : fail(error);
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2)
        return fail(QString::fromLatin1(kUsage));
    if (!sl::initialize())
        return fail(QStringLiteral("the cryptography libraries did not start"));
    const QString command = args.at(1);
    const QMap<QString, QStringList> allowed{
        {QStringLiteral("master-keys"), {QStringLiteral("master")}},
        {QStringLiteral("authorize"),
         {QStringLiteral("master"), QStringLiteral("server-public"), QStringLiteral("host"), QStringLiteral("sequence"),
          QStringLiteral("days"), QStringLiteral("out")}},
        {QStringLiteral("revoke"),
         {QStringLiteral("master"), QStringLiteral("server-public"), QStringLiteral("sequence"), QStringLiteral("revocations")}},
        {QStringLiteral("app-master"), {QStringLiteral("master"), QStringLiteral("host"), QStringLiteral("out")}},
        {QStringLiteral("server-keys"), {QStringLiteral("data"), QStringLiteral("passphrase-file")}},
        {QStringLiteral("serve"),
         {QStringLiteral("data"), QStringLiteral("port"), QStringLiteral("tls-cert"), QStringLiteral("tls-key"), QStringLiteral("listen"),
          QStringLiteral("passphrase-file"), QStringLiteral("test-item-pages")}},
        {QStringLiteral("queue"), {QStringLiteral("data")}},
        {QStringLiteral("publish"), {QStringLiteral("data"), QStringLiteral("id")}},
        {QStringLiteral("hold"), {QStringLiteral("data"), QStringLiteral("id")}},
        {QStringLiteral("remove"), {QStringLiteral("data"), QStringLiteral("id")}},
        {QStringLiteral("close-dispute"), {QStringLiteral("data"), QStringLiteral("id")}},
    };
    if (!allowed.contains(command))
        return fail(QString::fromLatin1(kUsage));
    QString error;
    const std::optional<QMap<QString, QString>> map = options(args, allowed.value(command), &error);
    if (!map)
        return fail(error);
    if (command == QLatin1String("master-keys"))
        return masterKeysCommand(*map);
    if (command == QLatin1String("authorize"))
        return authorizeCommand(*map);
    if (command == QLatin1String("revoke"))
        return revokeCommand(*map);
    if (command == QLatin1String("app-master"))
        return appMasterCommand(*map);
    if (command == QLatin1String("server-keys"))
        return serverKeysCommand(*map);
    if (command == QLatin1String("serve"))
        return serveCommand(*map);
    if (command == QLatin1String("queue"))
        return queueCommand(*map);
    if (command == QLatin1String("publish"))
        return statusCommand(*map, ServerStore::Status::Published);
    if (command == QLatin1String("hold"))
        return statusCommand(*map, ServerStore::Status::Held);
    if (command == QLatin1String("remove"))
        return statusCommand(*map, ServerStore::Status::Removed);
    return closeDisputeCommand(*map);
}
