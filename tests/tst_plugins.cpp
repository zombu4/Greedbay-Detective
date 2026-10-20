// Plugins (DESIGN 5, PLAN 15): the manifest rules, installing and removing,
// the sandbox, the time limit, failures, permissions and the four built-in
// examples; and the export files (core/ExportFiles, lookup/LookupExport).
#include "core/ExportFiles.h"
#include "core/Settings.h"
#include "core/WorkFolder.h"
#include "lookup/LookupExport.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginHost.h"
#include "plugins/PluginManifest.h"
#include "support/TestFiles.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>
#include <QtCore/private/qzipwriter_p.h>

#include <optional>
#include <tuple>

using namespace app::plugins;
using app::lookup::Bucket;
using app::lookup::LookupResult;
using app::lookup::Row;

namespace {

QByteArray manifest(const QString &id, const QString &type, const QStringList &permissions)
{
    return QJsonDocument(QJsonObject{{"id", id},
                                     {"name", "Test " + id},
                                     {"version", "1.0"},
                                     {"author", "Tests"},
                                     {"pluginApiVersion", 1},
                                     {"type", type},
                                     {"permissions", QJsonArray::fromStringList(permissions)}})
        .toJson();
}

// A lookup with a listing and three sold rows (all same item, counted).
LookupResult lookupResult(std::optional<int> feedback)
{
    LookupResult result;
    result.item.itemId = QStringLiteral("100000000001");
    result.item.title = QStringLiteral("Acme Q9 signal tracer");
    result.item.priceCents = 10000;
    result.item.shippingCents = 1500;
    result.item.currency = QStringLiteral("USD");
    result.item.sellerName = QStringLiteral("seller-a");
    result.item.sellerFeedbackCount = feedback;
    result.identity.brandName = QStringLiteral("Acme");
    result.identity.model = QStringLiteral("Q9");
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QString old = QDate::currentDate().addDays(-200).toString(Qt::ISODate);
    for (const auto &[id, title, cents, date] :
         {std::tuple{"200000000001", "Acme Q9 tracer", 9000, today}, std::tuple{"200000000002", "Acme Q9 replica, \"new\"", 4000, today},
          std::tuple{"200000000003", "Acme Q9 tracer, boxed", 12000, old}}) {
        app::lookup::SearchRecord record;
        record.itemId = QLatin1String(id);
        record.title = QLatin1String(title);
        record.priceCents = cents;
        record.shippingCents = 1000;
        record.soldDate = date;
        record.confirmed = true;
        result.sold.append(record);
    }
    return result;
}

QList<Row> sameItemRows(const LookupResult &result)
{
    QList<Row> rows;
    for (qsizetype i = 0; i < result.sold.size(); ++i) {
        Row row;
        row.source = Row::Source::Sold;
        row.index = i;
        row.match.bucket = Bucket::SameItem;
        row.match.reasons = {QStringLiteral("same item")};
        row.included = true;
        rows.append(row);
    }
    return rows;
}

} // namespace

class TestPlugins : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    std::optional<app::WorkFolder> m_folder;
    // Writes an installable plugin folder; its path.
    QString pluginFolder(const QString &id, const QString &type, const QStringList &permissions, const QByteArray &script);
    // Installs and enables a plugin; false when either step fails.
    bool installEnabled(const PluginCatalog &catalog, const QString &id, const QString &type, const QStringList &permissions,
                        const QByteArray &script);

private slots:
    void init();
    void manifestRules();
    void builtInExamples();
    void installAndRemove();
    void installFromZip();
    void sandboxHasNoAccess();
    void timeLimitStopsALoop();
    void failuresDisable();
    void permissionsLimitTheData();
    void examplesWork();
    void exportFiles();
};

void TestPlugins::init()
{
    QVERIFY(m_dir.isValid());
    const QString root = m_dir.path() + QStringLiteral("/") + QString::number(QDateTime::currentMSecsSinceEpoch());
    QString error;
    m_folder = app::WorkFolder::open(root, QStringLiteral(TEST_APP_NAME), &error);
    QVERIFY2(m_folder, qPrintable(error));
    app::Settings settings(m_folder->settingsPath());
    QVERIFY(settings.setPluginCallTimeLimitMs(300));
    QVERIFY(settings.sync());
}

QString TestPlugins::pluginFolder(const QString &id, const QString &type, const QStringList &permissions,
                                  const QByteArray &script)
{
    const QString folder = m_dir.path() + QStringLiteral("/source-") + id;
    if (!QDir().mkpath(folder) || !writeTestFile(folder + QStringLiteral("/manifest.json"), manifest(id, type, permissions))
        || !writeTestFile(folder + QStringLiteral("/main.js"), script))
        return {};
    return folder;
}

bool TestPlugins::installEnabled(const PluginCatalog &catalog, const QString &id, const QString &type,
                                 const QStringList &permissions, const QByteArray &script)
{
    QString error;
    const QString folder = pluginFolder(id, type, permissions, script);
    if (folder.isEmpty() || !catalog.install(folder, &error) || !catalog.setEnabled(id, true, &error)) {
        qWarning("%s", qPrintable(error));
        return false;
    }
    return true;
}

void TestPlugins::manifestRules()
{
    QString error;
    const std::optional<PluginManifest> good = PluginManifest::fromJson(manifest("a-1", "flagger", {"listing"}), &error);
    QVERIFY2(good, qPrintable(error));
    QCOMPARE(good->function(), QStringLiteral("flags"));
    QCOMPARE(good->typeName(), QStringLiteral("flagger"));
    // Refused: an unknown field, another API version, a bad id, an exporter
    // without "export", "export" on another type, a matcher without "rows",
    // an unknown permission, a name too long.
    QJsonObject extra = QJsonDocument::fromJson(manifest("a-1", "flagger", {})).object();
    extra.insert("homepage", "x");
    QVERIFY(!PluginManifest::fromJson(QJsonDocument(extra).toJson(), &error));
    QVERIFY2(error.contains("homepage"), qPrintable(error));
    QJsonObject version = QJsonDocument::fromJson(manifest("a-1", "flagger", {})).object();
    version.insert("pluginApiVersion", 2);
    QVERIFY(!PluginManifest::fromJson(QJsonDocument(version).toJson(), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("A_1", "flagger", {}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("-a", "flagger", {}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "exporter", {"rows"}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "flagger", {"export"}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "matcher", {"listing"}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "valuator", {}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "flagger", {"files"}), &error));
    QVERIFY(!PluginManifest::fromJson(manifest("a", "scraper", {}), &error));
    QJsonObject longName = QJsonDocument::fromJson(manifest("a", "flagger", {})).object();
    longName.insert("name", QString(81, QLatin1Char('n')));
    QVERIFY(!PluginManifest::fromJson(QJsonDocument(longName).toJson(), &error));
    QVERIFY(!PluginManifest::fromJson("not json", &error));
}

void TestPlugins::builtInExamples()
{
    const PluginCatalog catalog(*m_folder);
    QStringList problems;
    const QList<PluginInfo> plugins = catalog.plugins(&problems);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join('\n')));
    QStringList ids;
    QStringList types;
    for (const PluginInfo &info : plugins) {
        ids.append(info.manifest.id);
        types.append(info.manifest.typeName());
        QVERIFY(info.builtIn);
        QVERIFY(!info.enabled); // off until the user turns it on
    }
    QCOMPARE(ids, QStringList({"example-csv-export", "example-few-ratings", "example-no-reproductions", "example-recent-median"}));
    types.sort();
    QCOMPARE(types, QStringList({"exporter", "flagger", "matcher", "valuator"}));
    QString error;
    QVERIFY(!catalog.remove(QStringLiteral("example-few-ratings"), &error));
    QVERIFY(catalog.setEnabled(QStringLiteral("example-few-ratings"), true, &error));
    QVERIFY(catalog.find(QStringLiteral("example-few-ratings"))->enabled);
    QVERIFY(!catalog.setEnabled(QStringLiteral("no-such-plugin"), true, &error));
}

void TestPlugins::installAndRemove()
{
    const PluginCatalog catalog(*m_folder);
    QString error;
    const QString source = pluginFolder("my-flags", "flagger", {"listing"}, "function flags(input) { return null; }");
    QVERIFY(!source.isEmpty());
    QVERIFY2(catalog.install(source, &error), qPrintable(error));
    std::optional<PluginInfo> info = catalog.find(QStringLiteral("my-flags"));
    QVERIFY(info);
    QVERIFY(!info->builtIn);
    QVERIFY(!info->enabled);
    QCOMPARE(QDir::cleanPath(info->location), QDir::cleanPath(m_folder->pluginsDir() + QStringLiteral("/my-flags")));
    // Enabled, then installed again: off again.
    QVERIFY(catalog.setEnabled(QStringLiteral("my-flags"), true, &error));
    QVERIFY(catalog.install(source, &error));
    QVERIFY(!catalog.find(QStringLiteral("my-flags"))->enabled);
    // A built-in example's id is refused; so is a link to a folder.
    QVERIFY(!catalog.install(pluginFolder("example-few-ratings", "flagger", {}, "function flags() { return null; }"), &error));
    QVERIFY2(error.contains("built-in"), qPrintable(error));
    const QString link = m_dir.path() + QStringLiteral("/link-to-plugin");
    QVERIFY(makeDirectoryLink(source, link));
    QVERIFY(!catalog.install(link, &error));
    // A script larger than the limit.
    const QString big = pluginFolder("big", "flagger", {}, QByteArray(kMaxScriptBytes + 1, ' '));
    QVERIFY(!catalog.install(big, &error));
    // Removed: both files and the folder.
    QVERIFY2(catalog.remove(QStringLiteral("my-flags"), &error), qPrintable(error));
    QVERIFY(!catalog.find(QStringLiteral("my-flags")));
    QVERIFY(!QFileInfo::exists(m_folder->pluginsDir() + QStringLiteral("/my-flags")));
    // Another file in the folder: the plugin goes, the folder stays.
    QVERIFY(catalog.install(source, &error));
    QVERIFY(writeTestFile(m_folder->pluginsDir() + QStringLiteral("/my-flags/notes.txt"), "mine"));
    QVERIFY(!catalog.remove(QStringLiteral("my-flags"), &error));
    QVERIFY2(error.contains("other files"), qPrintable(error));
    QVERIFY(!catalog.find(QStringLiteral("my-flags")));
    QVERIFY(QFileInfo::exists(m_folder->pluginsDir() + QStringLiteral("/my-flags/notes.txt")));
}

void TestPlugins::installFromZip()
{
    const PluginCatalog catalog(*m_folder);
    QString error;
    // At the top of the zip.
    const QString top = m_dir.path() + QStringLiteral("/top.zip");
    {
        QZipWriter zip(top);
        zip.addFile(QStringLiteral("manifest.json"), manifest("zipped", "flagger", {"listing"}));
        zip.addFile(QStringLiteral("main.js"), "function flags(input) { return null; }");
        zip.addFile(QStringLiteral("README.txt"), "not read");
        zip.close();
        QCOMPARE(zip.status(), QZipWriter::NoError);
    }
    QVERIFY2(catalog.install(top, &error), qPrintable(error));
    QVERIFY(catalog.find(QStringLiteral("zipped")));
    QVERIFY(!QFileInfo::exists(m_folder->pluginsDir() + QStringLiteral("/zipped/README.txt")));
    // Inside one folder.
    const QString nested = m_dir.path() + QStringLiteral("/nested.zip");
    {
        QZipWriter zip(nested);
        zip.addFile(QStringLiteral("nested-plugin/manifest.json"), manifest("nested", "flagger", {}));
        zip.addFile(QStringLiteral("nested-plugin/main.js"), "function flags(input) { return null; }");
        zip.close();
    }
    QVERIFY2(catalog.install(nested, &error), qPrintable(error));
    QVERIFY(catalog.find(QStringLiteral("nested")));
    // Two manifests, or no main.js: refused, nothing written.
    const QString two = m_dir.path() + QStringLiteral("/two.zip");
    {
        QZipWriter zip(two);
        zip.addFile(QStringLiteral("manifest.json"), manifest("two", "flagger", {}));
        zip.addFile(QStringLiteral("other/manifest.json"), manifest("two", "flagger", {}));
        zip.addFile(QStringLiteral("main.js"), "function flags() { return null; }");
        zip.close();
    }
    QVERIFY(!catalog.install(two, &error));
    const QString noScript = m_dir.path() + QStringLiteral("/noscript.zip");
    {
        QZipWriter zip(noScript);
        zip.addFile(QStringLiteral("manifest.json"), manifest("noscript", "flagger", {}));
        zip.close();
    }
    QVERIFY(!catalog.install(noScript, &error));
    QVERIFY(!catalog.find(QStringLiteral("noscript")));
    // Not a zip at all.
    const QString fake = m_dir.path() + QStringLiteral("/fake.zip");
    QVERIFY(writeTestFile(fake, "plain text"));
    QVERIFY(!catalog.install(fake, &error));
}

void TestPlugins::sandboxHasNoAccess()
{
    const PluginCatalog catalog(*m_folder);
    // Every name below would give a script a way out; none may exist on the
    // global object (`this` at the top of a script). The script also
    // replaces JSON.parse: the host took its own copy first.
    QVERIFY(installEnabled(catalog, "probe", "flagger", {"listing"}, R"(
        var global = this;
        var found = ["XMLHttpRequest", "Qt", "console", "require", "print", "gc", "qsTr", "fetch", "process", "importScripts",
                     "WebSocket", "setTimeout"].filter(function (name) { return typeof global[name] !== "undefined"; });
        JSON.parse = function () { throw new Error("replaced"); };
        function flags(input) {
            return found.length === 0 ? null : [{text: "found " + found.join(" "), kind: "warn"}];
        }
    )"));
    PluginHost host(catalog, [] { return 2000; });
    host.reload();
    const LookupResult result = lookupResult(5);
    const QList<PluginFlag> flags = host.flags(result, sameItemRows(result));
    const QStringList failures = host.takeFailures();
    QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    QVERIFY2(flags.isEmpty(), qPrintable(flags.isEmpty() ? QString() : flags.first().text));
}

void TestPlugins::timeLimitStopsALoop()
{
    const PluginCatalog catalog(*m_folder);
    QVERIFY(installEnabled(catalog, "loops", "flagger", {"listing"}, "function flags(input) { for (;;) {} }"));
    QVERIFY(installEnabled(catalog, "loads-forever", "flagger", {}, "while (true) {} function flags() { return null; }"));
    PluginHost host(catalog, [] { return 300; });
    QElapsedTimer timer;
    timer.start();
    host.reload();
    const LookupResult result = lookupResult(5);
    QVERIFY(host.flags(result, sameItemRows(result)).isEmpty());
    // Both stopped after their limit, not after minutes.
    QVERIFY2(timer.elapsed() < 10000, qPrintable(QString::number(timer.elapsed())));
    const QStringList failures = host.takeFailures();
    QCOMPARE(failures.size(), 2);
    QVERIFY2(failures.join(' ').contains("300 ms"), qPrintable(failures.join('\n')));
    // Disabled, with the reason kept for the Plugins screen.
    QVERIFY(!catalog.find(QStringLiteral("loops"))->enabled);
    QVERIFY(catalog.find(QStringLiteral("loops"))->failure.contains("300 ms"));
    QVERIFY(!catalog.find(QStringLiteral("loads-forever"))->enabled);
}

void TestPlugins::failuresDisable()
{
    const PluginCatalog catalog(*m_folder);
    QVERIFY(installEnabled(catalog, "throws", "flagger", {}, "function flags(input) { throw new Error('broken on purpose'); }"));
    QVERIFY(installEnabled(catalog, "wrong-shape", "flagger", {}, "function flags(input) { return [{text: 5}]; }"));
    QVERIFY(installEnabled(catalog, "no-function", "flagger", {}, "var nothing = 1;"));
    QVERIFY(installEnabled(catalog, "bad-syntax", "flagger", {}, "function flags( {"));
    QVERIFY(installEnabled(catalog, "bad-row", "matcher", {"rows"},
                           "function match(input) { return [{row: 99, bucket: 'lot', reason: 'x'}]; }"));
    QVERIFY(installEnabled(catalog, "fine", "flagger", {}, "function flags(input) { return [{text: 'ok', kind: 'info'}]; }"));
    PluginHost host(catalog, [] { return 2000; });
    host.reload();
    const LookupResult result = lookupResult(5);
    QList<Row> rows = sameItemRows(result);
    const QList<Row> before = rows;
    host.applyMatchers(result, &rows);
    // A wrong answer changes nothing.
    for (qsizetype i = 0; i < rows.size(); ++i)
        QCOMPARE(rows.at(i).match.bucket, before.at(i).match.bucket);
    const QList<PluginFlag> flags = host.flags(result, rows);
    // The good one still runs.
    QCOMPARE(flags.size(), 1);
    QCOMPARE(flags.first().text, QStringLiteral("ok"));
    const QString failures = host.takeFailures().join('\n');
    for (const char *id : {"throws", "wrong-shape", "no-function", "bad-syntax", "bad-row"})
        QVERIFY2(!catalog.find(QLatin1String(id))->enabled, id);
    QVERIFY2(failures.contains("broken on purpose"), qPrintable(failures));
    QVERIFY(catalog.find(QStringLiteral("fine"))->enabled);
}

void TestPlugins::permissionsLimitTheData()
{
    const LookupResult result = lookupResult(5);
    const QList<Row> rows = sameItemRows(result);
    QString error;
    const PluginManifest none = *PluginManifest::fromJson(manifest("n", "flagger", {}), &error);
    const QJsonObject bare = PluginHost::inputFor(none, result, rows);
    QCOMPARE(bare.keys(), QStringList({"apiVersion"}));
    const PluginManifest all = *PluginManifest::fromJson(manifest("a", "flagger", {"listing", "identity", "rows"}), &error);
    const QJsonObject full = PluginHost::inputFor(all, result, rows);
    QCOMPARE(full.value("listing").toObject().value("sellerFeedbackCount").toInt(), 5);
    QCOMPARE(full.value("identity").toObject().value("model").toString(), QStringLiteral("Q9"));
    QCOMPARE(full.value("rows").toArray().size(), 3);
    QCOMPARE(full.value("rows").toArray().first().toObject().value("totalCents").toInt(), 10000);
    QCOMPARE(full.value("rows").toArray().first().toObject().value("bucket").toString(), QStringLiteral("same_item"));
}

void TestPlugins::examplesWork()
{
    const PluginCatalog catalog(*m_folder);
    QString error;
    for (const char *id : {"example-csv-export", "example-few-ratings", "example-no-reproductions", "example-recent-median"})
        QVERIFY2(catalog.setEnabled(QLatin1String(id), true, &error), qPrintable(error));
    PluginHost host(catalog, [] { return 2000; });
    host.reload();
    QVERIFY(host.takeFailures().isEmpty());

    // Few ratings: a warning below 10, nothing at 10 or more or without a count.
    LookupResult few = lookupResult(3);
    QList<PluginFlag> flags = host.flags(few, sameItemRows(few));
    QCOMPARE(flags.size(), 1);
    QCOMPARE(flags.first().text, QStringLiteral("The seller has only 3 ratings"));
    QCOMPARE(flags.first().kind, QStringLiteral("warn"));
    LookupResult many = lookupResult(250);
    QVERIFY(host.flags(many, sameItemRows(many)).isEmpty());
    LookupResult unknown = lookupResult(std::nullopt);
    QVERIFY(host.flags(unknown, sameItemRows(unknown)).isEmpty());

    // No reproductions: the replica leaves the same-item rows.
    QList<Row> rows = sameItemRows(few);
    host.applyMatchers(few, &rows);
    QCOMPARE(rows.at(1).match.bucket, Bucket::NoMatch);
    QVERIFY(!rows.at(1).included);
    QVERIFY(rows.at(1).match.reasons.last().contains("replica"));
    QCOMPARE(rows.at(0).match.bucket, Bucket::SameItem);

    // Recent median: the counted sold rows of the last 30 days (the replica
    // is no longer counted; the 200-day-old sale is too old): $100.00.
    const QList<PluginValue> values = host.values(few, rows);
    QCOMPARE(values.size(), 1);
    QCOMPARE(values.first().cents, 10000);
    QCOMPARE(values.first().note, QStringLiteral("1 sale"));

    // CSV export: a header and one line per row, quoted where needed.
    QCOMPARE(host.exporters().size(), 1);
    const std::optional<PluginExport> file = host.exportLookup(QStringLiteral("example-csv-export"), few, rows, &error);
    QVERIFY2(file, qPrintable(error));
    QCOMPARE(file->fileName, QStringLiteral("rows-100000000001.csv"));
    const QList<QByteArray> lines = file->data.split('\n');
    QCOMPARE(lines.first().trimmed(), QByteArray("where,item,total_usd,ebay_condition,date,counted,bucket,title"));
    QVERIFY2(file->data.contains("\"Acme Q9 replica, \"\"new\"\"\""), file->data.constData());
    QVERIFY(host.takeFailures().isEmpty());
}

void TestPlugins::exportFiles()
{
    const QString folder = m_dir.path() + QStringLiteral("/downloads");
    QVERIFY(QDir().mkpath(folder));
    QString error;
    const std::optional<QString> first = app::saveExport(folder, QStringLiteral("a.csv"), "one", &error);
    const std::optional<QString> second = app::saveExport(folder, QStringLiteral("a.csv"), "two", &error);
    QVERIFY2(first && second, qPrintable(error));
    QCOMPARE(QFileInfo(*first).fileName(), QStringLiteral("a.csv"));
    QCOMPARE(QFileInfo(*second).fileName(), QStringLiteral("a-2.csv"));
    QCOMPARE(readTestFile(*first), QByteArray("one")); // never replaced
    QVERIFY(!app::saveExport(folder, QStringLiteral("../a.csv"), "x", &error));
    QVERIFY(!app::saveExport(folder, QStringLiteral("sub/a.csv"), "x", &error));
    QVERIFY(!app::saveExport(folder + QStringLiteral("/missing"), QStringLiteral("a.csv"), "x", &error));

    // The lookups as CSV: RFC 4180 quoting, CRLF.
    app::lookup::LookupSummary summary;
    summary.id = 7;
    summary.createdAt = QDateTime(QDate(2026, 10, 20), QTime(12, 0), QTimeZone::UTC);
    summary.title = QStringLiteral("Acme \"Q9\", boxed");
    summary.listingTotalCents = 11500;
    const QByteArray csv = app::lookup::lookupsCsv({summary});
    QVERIFY(csv.startsWith("id,checked_utc,input,"));
    QVERIFY2(csv.contains("7,2026-10-20T12:00:00Z,,,\"Acme \"\"Q9\"\", boxed\",,,,11500,"), csv.constData());
    QVERIFY(csv.endsWith("\r\n"));
    // One lookup as JSON, with its rows.
    app::lookup::SavedLookup saved;
    saved.id = 7;
    saved.result = lookupResult(5);
    saved.rows = sameItemRows(saved.result);
    const QJsonObject json = app::lookup::lookupJson(saved);
    QCOMPARE(json.value("rows").toArray().size(), 3);
    QCOMPARE(json.value("listing").toObject().value("totalCents").toInt(), 11500);
    QVERIFY(QJsonDocument::fromJson(app::lookup::lookupsJson({saved})).isObject());
}

QTEST_GUILESS_MAIN(TestPlugins)
#include "tst_plugins.moc"
