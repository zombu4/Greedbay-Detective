// The matching engine (src/lookup/): the shipped data files, the user's
// known-models file, specifications, identity and queries for each of the
// owner's eight test cases, the bucket and condition of result rows (titles
// are invented and reproduce each junk pattern of those cases), and the value
// math. Brand and model names are those of the shipped data; every title,
// item number and price is invented.
#include "lookup/Identity.h"
#include "lookup/KnownModels.h"
#include "lookup/LookupReport.h"
#include "lookup/MatchWords.h"
#include "lookup/Matcher.h"
#include "lookup/Text.h"
#include "lookup/Value.h"
#include "support/TestFiles.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace app::lookup;

Q_DECLARE_METATYPE(app::lookup::Bucket)

namespace {

ItemRecord item(const QString &title, const QList<ItemRecord::Specific> &specifics = {})
{
    ItemRecord record;
    record.itemId = QStringLiteral("100000000099");
    record.title = title;
    record.specifics = specifics;
    return record;
}

} // namespace

class TestMatching : public QObject {
    Q_OBJECT

private:
    KnownModels m_known;
    MatchWords m_words;

private slots:
    void initTestCase();
    void dataFiles();
    void userFileSavesAndReloads();
    void userFileIsNotTrustedBlindly();
    void titleWords();
    void specifications_data();
    void specifications();
    void modelLike_data();
    void modelLike();
    void identityPerCase();
    void identityWithoutKnownBrand();
    void identityBrandScan();
    void identityCase11();
    void identityCase12();
    void searchNamesByEra();
    void ownListingIsNeverNotThisItem();
    void rows_data();
    void rows();
    void decisionPath();
    void descriptionFindings();
    void productNameWords();
    void valueOverAllRows();
    void partsKitIsNotCheapest();
    void heathZenithFamily();
    void likelyBroken();
    void shippingCheck();
};

void TestMatching::initTestCase()
{
    QString error;
    const std::optional<KnownModels> known = KnownModels::builtIn(&error);
    QVERIFY2(known, qPrintable(error));
    m_known = *known;
    const std::optional<MatchWords> words = MatchWords::builtIn(&error);
    QVERIFY2(words, qPrintable(error));
    m_words = *words;
}

void TestMatching::dataFiles()
{
    // The shipped data reads with every source required (builtIn did).
    QCOMPARE(m_known.brands().size(), 92);
    QVERIFY(m_known.findModel(QStringLiteral("marconi"), QStringLiteral("2022D")));
    // The bench-scope makers (owner approval 2026-10-20): a product line is
    // the product name, never a family; respellings only match.
    const KnownModel *plugin = m_known.findModel(QStringLiteral("tektronix"), QStringLiteral("7A26"));
    QVERIFY(plugin);
    QCOMPARE(plugin->productName, QStringLiteral("7000 Series Plug-in"));
    QVERIFY(plugin->family.isEmpty());
    QCOMPARE(plugin->firstYear, 1974);
    QCOMPARE(plugin->lastYear, 1991);
    QVERIFY(m_known.findModel(QStringLiteral("fnirsi"), QStringLiteral("1014D")));
    QVERIFY(m_known.findModel(QStringLiteral("rigol"), QStringLiteral("DS70000")));
    const Brand *lecroy = m_known.brandByName(QStringLiteral("LeCroy"));
    QVERIFY(lecroy);
    QCOMPARE(lecroy->id, QStringLiteral("lecroy"));
    for (const BrandName &name : lecroy->names)
        QCOMPARE(name.search, name.name == QStringLiteral("Teledyne LeCroy"));
    QVERIFY(m_known.findModel(QStringLiteral("racaldana"), QStringLiteral("1992")));
    // Years (owner decision 2026-10-19): first and optional last year.
    const KnownModel *c64 = m_known.findModel(QStringLiteral("commodore"), QStringLiteral("64"));
    QVERIFY(c64);
    QCOMPARE(c64->firstYear, 1982);
    QCOMPARE(c64->lastYear, 0);
    QCOMPARE(c64->category, QStringLiteral("computer"));
    // Misspellings seen in titles are match-only names of their brand.
    const Brand *heathit = m_known.brandByName(QStringLiteral("HealthKit"));
    QVERIFY(heathit);
    QCOMPARE(heathit->id, QStringLiteral("heathkit"));
    for (const BrandName &name : heathit->names) {
        if (name.name == QStringLiteral("HealthKit"))
            QVERIFY(!name.search);
    }
    QCOMPARE(m_known.brandByName(QStringLiteral("etco"))->id, QStringLiteral("eico"));
    // "Knight" alone never makes the brand.
    QVERIFY(!m_known.brandByName(QStringLiteral("Knight")));
    QCOMPARE(m_known.brandByName(QStringLiteral("Allied Knight"))->id, QStringLiteral("knight"));
    QVERIFY(!m_known.findModel(QStringLiteral("sencore"), QStringLiteral("PR5700")));
    // Seller spellings replaced by the sourced ones (owner decision 2026-10-20).
    for (const char *code : {"O-12", "IO-12", "IO-30"}) {
        QVERIFY2(m_known.findModel(QStringLiteral("heathkit"), QLatin1String(code)), code);
    }
    QVERIFY(m_known.findModel(QStringLiteral("tektronix"), QStringLiteral("TSG131A")));
    QVERIFY(!m_known.findModel(QStringLiteral("tektronix"), QStringLiteral("TTSG131A")));
    QVERIFY(!m_known.findModel(QStringLiteral("heathkit"), QStringLiteral("10W-12")));
    // "Heath Kit" is a name of Heathkit, never searched (S8).
    const Brand *heathKit = m_known.brandByName(QStringLiteral("heath kit"));
    QVERIFY(heathKit);
    QCOMPARE(heathKit->id, QStringLiteral("heathkit"));
    // The shipped data holds only what the app reads: no "source" or
    // "decision" fields at any level, in either file.
    QFile builtInFile(QStringLiteral(":/lookup/known-models.json"));
    QVERIFY(builtInFile.open(QIODevice::ReadOnly));
    const QByteArray builtInJson = builtInFile.readAll();
    QVERIFY(!builtInJson.contains("\"source\""));
    QVERIFY(!builtInJson.contains("\"decision\""));
    QFile wordsFile(QStringLiteral(":/lookup/match-words.json"));
    QVERIFY(wordsFile.open(QIODevice::ReadOnly));
    QVERIFY(!wordsFile.readAll().contains("\"source\""));
    QString error;
    const QByteArray badYear = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true}]}],
        "models":[{"brand":"x","model":"M7X","year":{"first":1990,"last":1980}}]})";
    QVERIFY(!KnownModels::fromJson(badYear, &error));
    QVERIFY2(error.contains(QStringLiteral("year")), qPrintable(error));
    const QByteArray firstYear = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true}]}],
        "models":[{"brand":"x","model":"M7X","year":{"first":1990}}]})";
    QVERIFY2(KnownModels::fromJson(firstYear, &error), qPrintable(error));
    const QByteArray lastYear = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true}]}],
        "models":[{"brand":"x","model":"M7X","year":{"first":1980,"last":1993}}]})";
    const std::optional<KnownModels> withLast = KnownModels::fromJson(lastYear, &error);
    QVERIFY2(withLast, qPrintable(error));
    QCOMPARE(withLast->findModel(QStringLiteral("x"), QStringLiteral("M7X"))->lastYear, 1993);
    // Word lists: a missing list is refused.
    QVERIFY(!MatchWords::fromJson(R"({"format":1})", &error));
}

void TestMatching::userFileSavesAndReloads()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/known-models.json");
    KnownModels known = m_known;
    QString error;
    QVERIFY2(known.addUserFile(path, &error), qPrintable(error)); // no file yet: nothing added
    // A known brand by an alias, a known model: only the listing is new.
    QVERIFY2(known.saveConfirmed(path, QStringLiteral("100000000001"), {QStringLiteral("Marconi"), QStringLiteral("2022"), QString()},
                                 &error),
             qPrintable(error));
    // A new brand and model.
    QVERIFY2(known.saveConfirmed(path, QStringLiteral("100000000002"), {QStringLiteral("Acme"), QStringLiteral("M7-X"),
                                                                         QStringLiteral("Bench Meter")}, &error),
             qPrintable(error));
    // A known brand with a new model: the user's file refers to the built-in brand.
    QVERIFY2(known.saveConfirmed(path, QStringLiteral("100000000003"), {QStringLiteral("sencore"), QStringLiteral("LC800"), QString()},
                                 &error),
             qPrintable(error));
    const QJsonObject saved = QJsonDocument::fromJson(readTestFile(path)).object();
    QCOMPARE(saved.value("brands").toArray().size(), 1);
    QCOMPARE(saved.value("models").toArray().size(), 2);
    QCOMPARE(saved.value("listings").toObject().size(), 3);

    KnownModels again = m_known;
    QVERIFY2(again.addUserFile(path, &error), qPrintable(error));
    QCOMPARE(again.confirmedFor(QStringLiteral("100000000001"))->model, QStringLiteral("2022"));
    const Brand *acme = again.brandByName(QStringLiteral("acme"));
    QVERIFY(acme);
    const KnownModel *model = again.findModel(acme->id, QStringLiteral("m7x"));
    QVERIFY(model);
    QCOMPARE(model->model, QStringLiteral("M7-X"));
    QCOMPARE(model->productName, QStringLiteral("Bench Meter"));
    const KnownModel *lc800 = again.findModel(QStringLiteral("sencore"), QStringLiteral("LC800"));
    QVERIFY(lc800);
    QCOMPARE(lc800->model, QStringLiteral("LC800"));

    // The saved identity comes first for that listing (rule 7).
    ItemRecord listing = item(QStringLiteral("Marconi Instruments 10kHz-1000MHz generator"));
    listing.itemId = QStringLiteral("100000000001");
    const Identity identity = identify(listing, again, m_words);
    QVERIFY(identity.confident);
    QCOMPARE(identity.source, QStringLiteral("saved"));
    QCOMPARE(identity.model, QStringLiteral("2022"));
}

void TestMatching::userFileIsNotTrustedBlindly()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString path = tmp.path() + QStringLiteral("/known-models.json");
    QVERIFY(writeTestFile(path, "not json"));
    KnownModels known = m_known;
    QString error;
    QVERIFY(!known.addUserFile(path, &error));
    QVERIFY2(error.contains(QStringLiteral("damaged")), qPrintable(error));
    QVERIFY(!known.saveConfirmed(path, QStringLiteral("1"), {QStringLiteral("Acme"), QStringLiteral("M7X"), QString()}, &error));
    QCOMPARE(readTestFile(path), QByteArray("not json"));
    QVERIFY(!known.saveConfirmed(tmp.path() + QStringLiteral("/other.json"), QStringLiteral("1"),
                                 {QStringLiteral("Acme"), QString(), QString()}, &error));
    if (kCanLinkFiles) {
        const QString linked = tmp.path() + QStringLiteral("/linked.json");
        QVERIFY(QFile::link(path, linked));
        QVERIFY(!known.addUserFile(linked, &error));
        QVERIFY2(error.contains(QStringLiteral("link")), qPrintable(error));
    }
}

void TestMatching::titleWords()
{
    QCOMPARE(app::lookup::titleWords(QStringLiteral("Subaru 15208-AA100, oil filter (OEM) #IM13 w/ Leads.")),
             QStringList({"Subaru", "15208-AA100", "oil", "filter", "OEM", "IM13", "w/", "Leads"}));
    QCOMPARE(listWords(QStringLiteral("PARTS/REPAIR foot/feet P/N 407-732")),
             QStringList({"parts", "repair", "foot", "feet", "p/n", "407-732"}));
    QCOMPARE(normalizeCode(QStringLiteral("aa-100")), QStringLiteral("AA100"));
}

void TestMatching::specifications_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("word");
    QTest::addColumn<bool>("spec");
    // Owner rule 24 and case 7.
    QTest::newRow("DC-600kHz") << "synthesizer DC-600kHz" << "DC-600kHz" << true;
    QTest::newRow("GHz range") << "generator 10kHz-1.01GHz" << "10kHz-1.01GHz" << true;
    QTest::newRow("DC-600 KHZ") << "SYNTHESIZER DC-600 KHZ" << "DC-600" << true;
    QTest::newRow("0-1500V") << "Voltmeter 0-1500V" << "0-1500V" << true;
    QTest::newRow("10kHz-1000MHz") << "Instruments 10kHz-1000MHz" << "10kHz-1000MHz" << true;
    QTest::newRow("50kv") << "probe 50kv" << "50kv" << true;
    QTest::newRow("15 MEG") << "15 MEG input" << "15" << true;
    QTest::newRow("45%") << "duty 45%" << "45%" << true;
    QTest::newRow("10Hz-50") << "10Hz-50 MHz" << "10Hz-50" << true;
    QTest::newRow("AA-100") << "Heathkit AA-100" << "AA-100" << false;
    QTest::newRow("IM-13") << "Heathkit IM-13 VTVM" << "IM-13" << false;
    QTest::newRow("8904A") << "HP 8904A synthesizer" << "8904A" << false;
    QTest::newRow("2022D") << "Marconi 2022D" << "2022D" << false;
    // Brand-scan S1 and retro-scan R5, R9.
    QTest::newRow("10-WATT") << "amplifier 10-WATT" << "10-WATT" << true;
    QTest::newRow("2KW") << "linear amplifier 2KW" << "2KW" << true;
    QTest::newRow("2M/6M") << "transceiver 2M/6M" << "2M/6M" << true;
    QTest::newRow("3.5MM") << "jack 3.5MM" << "3.5MM" << true;
    QTest::newRow("2-CHANNEL") << "scope 2-CHANNEL" << "2-CHANNEL" << true;
    QTest::newRow("4CH") << "scope 4CH" << "4CH" << true;
    QTest::newRow("2-PORT") << "analyzer 2-PORT" << "2-PORT" << true;
    QTest::newRow("16BIT") << "generator 16BIT" << "16BIT" << true;
    QTest::newRow("250MSa/s") << "generator 250MSa/s" << "250MSa/s" << true;
    QTest::newRow("1GSa/s") << "scope 1GSa/s" << "1GSa/s" << true;
    QTest::newRow("100MHz") << "scope 100MHz" << "100MHz" << true;
    QTest::newRow("20GSa/s") << "scope 20GSa/s" << "20GSa/s" << true;
    QTest::newRow("0-8V/3A") << "supply 0-8V/3A" << "0-8V/3A" << true;
    QTest::newRow("+-25V/1A") << "supply +-25V/1A" << "+-25V/1A" << true;
    QTest::newRow("0-15A") << "supply 0-15A" << "0-15A" << true;
    QTest::newRow("1HP") << "motor 1HP" << "1HP" << true;
    QTest::newRow("30DB") << "attenuator 30DB" << "30DB" << true;
    QTest::newRow("100MH") << "inductor 100MH" << "100MH" << true;
    QTest::newRow("30MF") << "capacitor 30MF" << "30MF" << true;
    QTest::newRow("10-CIRCUIT") << "switch 10-CIRCUIT" << "10-CIRCUIT" << true;
    QTest::newRow("5-20MM") << "fuse 5-20MM" << "5-20MM" << true;
    QTest::newRow("64GB") << "console 64GB" << "64GB" << true;
    QTest::newRow("1TB") << "drive 1TB" << "1TB" << true;
    QTest::newRow("512KB") << "memory 512KB" << "512KB" << true;
    QTest::newRow("11OZ") << "mug 11OZ" << "11OZ" << true;
    QTest::newRow("270GSM") << "print 270GSM" << "270GSM" << true;
    QTest::newRow("5FT") << "cable 5FT" << "5FT" << true;
    QTest::newRow("6PIN") << "cable 6PIN" << "6PIN" << true;
    QTest::newRow("14-PIN") << "socket 14-PIN" << "14-PIN" << true;
    // A and M count only in ranges and pairs; K sizes stay (ZX Spectrum 48K).
    QTest::newRow("30A alone") << "supply 30A" << "30A" << false;
    QTest::newRow("30A with volts") << "supply 0-25V 30A" << "30A" << true;
    QTest::newRow("6035A with volts") << "HP 6035A supply 0-500V" << "6035A" << false;
    QTest::newRow("48K") << "Sinclair ZX Spectrum 48K" << "48K" << false;
    QTest::newRow("10-30") << "Heathkit oscilloscope 10-30" << "10-30" << false;
    QTest::newRow("E3634A") << "HP E3634A 0-25V/7A" << "E3634A" << false;
    QTest::newRow("64/128") << "Enterprise 64/128" << "64/128" << false;
}

void TestMatching::modelLike_data()
{
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("word");
    QTest::addColumn<bool>("modelLike");
    // Connectors, buses, video and size specs, versions (S1, R5, R9, R10).
    QTest::newRow("APC-7") << "adapter APC-7 to N" << "APC-7" << false;
    QTest::newRow("10-IN-1") << "tool 10-IN-1" << "10-IN-1" << false;
    QTest::newRow("1080P") << "console HDMI 1080P" << "1080P" << false;
    QTest::newRow("3D") << "glasses 3D" << "3D" << false;
    QTest::newRow("2.4G") << "wireless 2.4G" << "2.4G" << false;
    QTest::newRow("DB9") << "cable DB9" << "DB9" << false;
    QTest::newRow("DB-25") << "cable DB-25" << "DB-25" << false;
    QTest::newRow("RS232") << "port RS232" << "RS232" << false;
    QTest::newRow("RS-423") << "port RS-423" << "RS-423" << false;
    QTest::newRow("S-100") << "bus S-100" << "S-100" << false;
    QTest::newRow("PS/2") << "keyboard PS/2" << "PS/2" << false;
    QTest::newRow("V1.15") << "ROM V1.15" << "V1.15" << false;
    QTest::newRow("REV.6A") << "board REV.6A" << "REV.6A" << false;
    QTest::newRow("MOS3.20") << "ROM MOS3.20" << "MOS3.20" << false;
    QTest::newRow("H150MM") << "stand H150MM" << "H150MM" << false;
    // Option codes (S2).
    QTest::newRow("Opt 1E5") << "HP 8904A Opt 1E5" << "1E5" << false;
    QTest::newRow("Opt 1E5 1EA") << "HP 8904A Opt 1E5 1EA" << "1EA" << false;
    QTest::newRow("Option H05") << "HP 8904A Option H05" << "H05" << false;
    // Models stay models.
    QTest::newRow("E3634A") << "HP E3634A supply" << "E3634A" << true;
    QTest::newRow("DSOX1204G") << "Keysight DSOX1204G scope" << "DSOX1204G" << true;
    QTest::newRow("IM-13") << "Heathkit IM-13" << "IM-13" << true;
    QTest::newRow("48K") << "ZX Spectrum 48K" << "48K" << true;
    QTest::newRow("model after option word") << "Opt 1E5 HP 8904A" << "8904A" << true;
}

void TestMatching::modelLike()
{
    QFETCH(QString, title);
    QFETCH(QString, word);
    QFETCH(bool, modelLike);
    const QStringList words = app::lookup::titleWords(title);
    const qsizetype at = words.indexOf(word);
    QVERIFY2(at >= 0, qPrintable(words.join(QLatin1Char('|'))));
    QCOMPARE(isModelLike(words, at, m_words), modelLike);
}

void TestMatching::specifications()
{
    QFETCH(QString, title);
    QFETCH(QString, word);
    QFETCH(bool, spec);
    const QStringList words = app::lookup::titleWords(title);
    const qsizetype at = words.indexOf(word);
    QVERIFY2(at >= 0, qPrintable(words.join(QLatin1Char('|'))));
    QCOMPARE(isSpecification(words, at, m_words), spec);
}

void TestMatching::identityPerCase()
{
    // Case 1: the model as the manufacturer writes it; one query, with the brand.
    Identity im13 = identify(item(QStringLiteral("Heathkit IM-13 VTVM Vacuum Tube Voltmeter 0-1500V")), m_known, m_words);
    QVERIFY(im13.confident);
    QCOMPARE(im13.brandName, QStringLiteral("Heathkit"));
    QCOMPARE(im13.model, QStringLiteral("IM-13"));
    QCOMPARE(im13.source, QStringLiteral("known model"));
    QCOMPARE(buildQueries(im13), QStringList({"Heathkit IM-13"}));

    // Case 2: "53" (numbers only) is no model and "53 Z" is never glued: ask.
    const Identity lc53 = identify(item(QStringLiteral("Sencore 53 Z Meter Capacitor Inductor Analyzer")), m_known, m_words);
    QVERIFY(!lc53.confident);
    QCOMPARE(lc53.brandName, QStringLiteral("Sencore"));
    QVERIFY(lc53.model.isEmpty());
    QVERIFY(buildQueries(lc53).isEmpty());
    QVERIFY(lc53.candidates.contains(QStringLiteral("LC53")));
    const Identity lc53User = identityFromUser({QStringLiteral("Sencore"), QStringLiteral("lc-53"), QString()}, m_known,
                                               QStringLiteral("user"));
    QCOMPARE(lc53User.model, QStringLiteral("LC53"));
    QCOMPARE(lc53User.productName, QStringLiteral("Z Meter"));
    QCOMPARE(buildQueries(lc53User), QStringList({"Sencore LC53"}));

    // Cases 3 and 5: PR570 and PR57 are different known models.
    const Identity pr570 = identify(item(QStringLiteral("Sencore PR570 Powerite II Isolation Transformer")), m_known, m_words);
    QCOMPARE(pr570.model, QStringLiteral("PR570"));
    QCOMPARE(pr570.productName, QStringLiteral("Powerite II"));
    const Identity pr57 = identify(item(QStringLiteral("Sencore PR57 Powerite Isolation Transformer")), m_known, m_words);
    QCOMPARE(pr57.model, QStringLiteral("PR57"));
    QCOMPARE(buildQueries(pr57), QStringList({"Sencore PR57"}));

    // Case 4.
    const Identity vg91 = identify(item(QStringLiteral("Sencore VG91 Universal Video Generator")), m_known, m_words);
    QCOMPARE(vg91.model, QStringLiteral("VG91"));

    // Case 6: written AA100 by the seller, searched as the manufacturer writes it.
    const Identity aa100 = identify(item(QStringLiteral("Heathkit AA100 Stereo Tube Amplifier")), m_known, m_words);
    QCOMPARE(aa100.model, QStringLiteral("AA-100"));
    QCOMPARE(buildQueries(aa100), QStringList({"Heathkit AA-100"}));
    QCOMPARE(aa100.notes.size(), 1);

    // Case 7: brand aliases; the frequency range is a specification.
    const Identity hp = identify(
        item(QStringLiteral("HEWLETT PACKARD HP 8904A SYNTHESIZER DC-600 KHZ 2 channels")), m_known, m_words);
    QVERIFY(hp.confident);
    QCOMPARE(hp.brandId, QStringLiteral("hp"));
    QCOMPARE(hp.model, QStringLiteral("8904A"));
    // Owner decisions 2026-10-19: successor names searched for HP test
    // equipment; no separate Hewlett-Packard query, but it still matches.
    QCOMPARE(buildQueries(hp), QStringList({"HP 8904A", "Hewlett Packard 8904A", "Agilent 8904A", "Keysight 8904A"}));
    QVERIFY(hp.matchNames.contains(QStringLiteral("Keysight")));
    QVERIFY(hp.matchNames.contains(QStringLiteral("Hewlett-Packard")));
    // An HP model the data does not mark as test equipment: no successor names.
    const Identity otherHp = identityFromUser({QStringLiteral("HP"), QStringLiteral("12C"), QString()}, m_known, QStringLiteral("user"));
    QCOMPARE(buildQueries(otherHp), QStringList({"HP 12C", "Hewlett Packard 12C"}));
    QVERIFY(!otherHp.matchNames.contains(QStringLiteral("Agilent")));
    QCOMPARE(matchRow(QStringLiteral("Agilent 12C calculator"), otherHp, m_words).bucket, Bucket::ToCheck);
    QCOMPARE(hp.notes.size(), 1);

    // Case 8: brand and specs only: no search, ask; the brand's known models
    // are suggested.
    const Identity marconi = identify(item(QStringLiteral("Marconi Instruments 10kHz-1000MHz generator")), m_known, m_words);
    QVERIFY(!marconi.confident);
    QCOMPARE(marconi.brandId, QStringLiteral("marconi"));
    QVERIFY(marconi.model.isEmpty());
    QCOMPARE(marconi.candidates.first(), QStringLiteral("2022"));
    QVERIFY(marconi.candidates.contains(QStringLiteral("2955B")));
    const Identity marconiUser = identityFromUser({QStringLiteral("Marconi Instruments"), QStringLiteral("2022"), QString()},
                                                  m_known, QStringLiteral("user"));
    QCOMPARE(buildQueries(marconiUser), QStringList({"Marconi Instruments 2022", "Marconi 2022"}));
    QCOMPARE(marconiUser.family, QStringList({"2022A", "2022C", "2022D", "2022E"}));

    // Owner decision 2026-10-19: a known digits-only model counts in the
    // title only right after a brand name (or its product name).
    const Identity racal = identify(item(QStringLiteral("Racal-Dana 1992 Universal Counter")), m_known, m_words);
    QVERIFY(racal.confident);
    QCOMPARE(racal.brandId, QStringLiteral("racaldana"));
    QCOMPARE(racal.model, QStringLiteral("1992"));
    QCOMPARE(buildQueries(racal), QStringList({"Racal-Dana 1992"}));
    const Identity racalYear = identify(item(QStringLiteral("Racal Dana counter manual printed 1992")), m_known, m_words);
    QCOMPARE(racalYear.brandId, QStringLiteral("racaldana"));
    QVERIFY(racalYear.model.isEmpty());
    QVERIFY(!racalYear.confident);
    // The longest known model wins ("Game Boy Color", not "Game Boy"); a
    // model of four title words.
    QCOMPARE(identify(item(QStringLiteral("Nintendo Game Boy Color handheld")), m_known, m_words).model,
             QStringLiteral("Game Boy Color"));
    QCOMPARE(identify(item(QStringLiteral("Radio Shack TRS-80 Color Computer 2")), m_known, m_words).model,
             QStringLiteral("TRS-80 Color Computer 2"));
    // A shorter model inside a longer one; the same machine under its
    // other code; the brand that has a known model in the title wins.
    QCOMPARE(identify(item(QStringLiteral("Nintendo Super NES console")), m_known, m_words).model, QStringLiteral("Super NES"));
    QCOMPARE(identify(item(QStringLiteral("Apple Macintosh Classic II")), m_known, m_words).model, QStringLiteral("Classic II"));
    QCOMPARE(identify(item(QStringLiteral("Commodore Amiga 2000 computer")), m_known, m_words).model, QStringLiteral("A2000"));
    QCOMPARE(identify(item(QStringLiteral("Commodore C64 computer")), m_known, m_words).model, QStringLiteral("64"));
    const Identity twoBrands = identify(item(QStringLiteral("Analogue Heathkit IM-13 meter")), m_known, m_words);
    QCOMPARE(twoBrands.brandId, QStringLiteral("heathkit"));
    QCOMPARE(twoBrands.model, QStringLiteral("IM-13"));
    // Earlier owners' names and misspellings name the brand.
    const Identity etco = identify(item(QStringLiteral("etco 666 tube tester")), m_known, m_words);
    QCOMPARE(etco.brandId, QStringLiteral("eico"));
    QCOMPARE(etco.model, QStringLiteral("666"));
    QCOMPARE(buildQueries(etco), QStringList({"EICO 666"}));
    QCOMPARE(identify(item(QStringLiteral("Heath Schlumberger SM-5238 counter")), m_known, m_words).brandId,
             QStringLiteral("heathkit"));
    // "Knight" alone is no brand; Allied Knight is.
    QVERIFY(identify(item(QStringLiteral("Moon Knight A7 trading card")), m_known, m_words).brandId != QStringLiteral("knight"));
    const Identity knight = identify(item(QStringLiteral("Allied Knight KG-625 VTVM")), m_known, m_words);
    QCOMPARE(knight.brandId, QStringLiteral("knight"));
    QCOMPARE(knight.model, QStringLiteral("KG-625"));

    // A seeded model written with a space in the listing title.
    QCOMPARE(identify(item(QStringLiteral("Heathkit IM 13 VTVM")), m_known, m_words).model, QStringLiteral("IM-13"));
    // "Unbranded" names no brand: the brand is looked for in the title.
    const Identity unbranded = identify(item(QStringLiteral("bench meter M7X"), {{QStringLiteral("Brand"), QStringLiteral("Unbranded")}}),
                                        m_known, m_words);
    QVERIFY(!unbranded.confident);
    QVERIFY(unbranded.brandId.isEmpty());

    // The item specifics name a model the title does not hold: not
    // confident; the app asks and suggests it (owner case 11).
    const Identity bySpecifics = identify(item(QStringLiteral("Vintage Sencore analyzer"),
                                               {{QStringLiteral("Brand"), QStringLiteral("Sencore")},
                                                {QStringLiteral("Model"), QStringLiteral("LC53")}}),
                                          m_known, m_words);
    QVERIFY(!bySpecifics.confident);
    QVERIFY(bySpecifics.candidates.contains(QStringLiteral("LC53")));
    // With the model in the title too, it is the known model.
    const Identity bothSay = identify(item(QStringLiteral("Vintage Sencore LC53 analyzer"),
                                           {{QStringLiteral("Brand"), QStringLiteral("Sencore")},
                                            {QStringLiteral("Model"), QStringLiteral("LC53")}}),
                                      m_known, m_words);
    QVERIFY(bothSay.confident);
    QCOMPARE(bothSay.model, QStringLiteral("LC53"));
}

void TestMatching::identityWithoutKnownBrand()
{
    // An unknown brand as written in the Brand specific; one model word.
    const Identity acme = identify(item(QStringLiteral("Acme M7-X bench meter 1000V"),
                                        {{QStringLiteral("Brand"), QStringLiteral("Acme")}}),
                                   m_known, m_words);
    QVERIFY(acme.confident);
    QCOMPARE(acme.model, QStringLiteral("M7-X"));
    QCOMPARE(buildQueries(acme), QStringList({"Acme M7-X"}));
    // Two model-like words: the one right after the brand (owner case 11).
    const Identity two = identify(item(QStringLiteral("Acme M7X B2 meter"), {{QStringLiteral("Brand"), QStringLiteral("Acme")}}),
                                  m_known, m_words);
    QVERIFY(two.confident);
    QCOMPARE(two.model, QStringLiteral("M7X"));
    // Two, neither right after the brand: ask.
    const Identity neither = identify(item(QStringLiteral("Acme meter M7X B2"), {{QStringLiteral("Brand"), QStringLiteral("Acme")}}),
                                      m_known, m_words);
    QVERIFY(!neither.confident);
    QCOMPARE(neither.candidates, QStringList({"M7X", "B2"}));
    // No brand at all: ask, even with a model word.
    const Identity none = identify(item(QStringLiteral("M7X bench meter")), m_known, m_words);
    QVERIFY(!none.confident);
    QVERIFY2(none.reason.contains(QStringLiteral("no brand")), qPrintable(none.reason));
}

void TestMatching::identityBrandScan()
{
    const QList<ItemRecord::Specific> acme{{QStringLiteral("Brand"), QStringLiteral("Acme")}};
    auto modelOf = [this, &acme](const char *title) {
        const Identity identity = identify(item(QLatin1String(title), acme), m_known, m_words);
        return identity.confident ? identity.model : QString();
    };
    // S17: digits only right after the brand (or "<brand> model") is the model.
    QCOMPARE(modelOf("Acme 2445 oscilloscope 150MHz"), QStringLiteral("2445"));
    QCOMPARE(modelOf("Acme model 2445 scope"), QStringLiteral("2445"));
    QCOMPARE(modelOf("Acme oscilloscope 2445"), QString());
    QCOMPARE(modelOf("Acme 100 W amplifier"), QString());
    // S18: a short letter word and a number written apart, right after the brand.
    QCOMPARE(modelOf("Acme TC 154 tube checker"), QStringLiteral("TC154"));
    QCOMPARE(modelOf("Acme No 5 tester"), QString());
    // S4: a seller's code at the end of the title.
    QCOMPARE(modelOf("Acme M7X meter QX7731"), QStringLiteral("M7X"));
    QCOMPARE(modelOf("Acme meter M7X QX7731"), QString());
    // S3: tube types give way to another candidate; alone they are the model.
    QCOMPARE(modelOf("Acme XR-5 amplifier with 12AX7 tubes"), QStringLiteral("XR-5"));
    QCOMPARE(modelOf("Acme 12AX7 vacuum tube"), QStringLiteral("12AX7"));
    // S2: option codes.
    QCOMPARE(modelOf("Acme M7X meter Opt 1E5 1EA"), QStringLiteral("M7X"));
    // S15: "Knight" alone names no brand.
    const Identity knight = identify(item(QStringLiteral("Space Knight costume VC77")), m_known, m_words);
    QVERIFY(!knight.confident);
    QVERIFY(knight.brandId.isEmpty());
    // S8: "Heath Kit" names Heathkit.
    const Identity heathKit = identify(item(QStringLiteral("Heath Kit IM-13 VTVM")), m_known, m_words);
    QCOMPARE(heathKit.brandId, QStringLiteral("heathkit"));
    QCOMPARE(heathKit.model, QStringLiteral("IM-13"));
}

void TestMatching::identityCase11()
{
    using Specifics = QList<ItemRecord::Specific>;
    auto identityOf = [this](const char *title, const Specifics &specifics) {
        return identify(item(QLatin1String(title), specifics), m_known, m_words);
    };
    const ItemRecord::Specific brand{QStringLiteral("Brand"), QStringLiteral("Acme")};
    // A seller's code in the MPN, not in the title: the title's model wins.
    const Identity sku = identityOf("Acme QX200 digital scope 100MHz 2CH dual channel",
                                    {brand, {QStringLiteral("MPN"), QStringLiteral("ZZ-b771")}, {QStringLiteral("Style"), QStringLiteral("100MHz 2CH")}});
    QVERIFY(sku.confident);
    QCOMPARE(sku.model, QStringLiteral("QX200"));
    QCOMPARE(sku.source, QStringLiteral("title"));
    QCOMPARE(buildQueries(sku), QStringList({"Acme QX200"}));
    // The title alone.
    QCOMPARE(identityOf("Acme QX200 scope", {brand}).model, QStringLiteral("QX200"));
    // Model vs MPN, both in the title, neither right after the brand: the
    // Model specific picks.
    const Identity both = identityOf("Acme digital scope QX200 KQ9",
                                     {brand, {QStringLiteral("Model"), QStringLiteral("QX200")}, {QStringLiteral("MPN"), QStringLiteral("KQ9")}});
    QVERIFY(both.confident);
    QCOMPARE(both.model, QStringLiteral("QX200"));
    QCOMPARE(both.source, QStringLiteral("item specifics"));
    // The MPN picks when the Model specific is not in the title.
    const Identity mpn = identityOf("Acme digital scope QX200 KQ9",
                                    {brand, {QStringLiteral("Model"), QStringLiteral("ZZ-B771")}, {QStringLiteral("MPN"), QStringLiteral("kq9")}});
    QCOMPARE(mpn.model, QStringLiteral("KQ9"));
    // A specific the title does not hold: ask, and suggest it.
    const Identity missing = identityOf("Acme digital scope 100MHz", {brand, {QStringLiteral("Model"), QStringLiteral("QX200")}});
    QVERIFY(!missing.confident);
    QVERIFY2(missing.reason.contains(QStringLiteral("QX200")), qPrintable(missing.reason));
    QVERIFY(missing.candidates.contains(QStringLiteral("QX200")));
    // Specifications are never models, from the title or the Style field.
    const Identity specs = identityOf("Acme scope 100MHz 2CH", {brand, {QStringLiteral("Style"), QStringLiteral("100MHz 2CH")}});
    QVERIFY(!specs.confident);
    QVERIFY(specs.model.isEmpty());
    // The Condition specific keeps eBay's short name only.
    const ItemRecord parsed = ItemRecord::fromJson(QJsonObject{
        {QStringLiteral("specifics"), QJsonArray{QJsonObject{{QStringLiteral("label"), QStringLiteral("Condition")},
                                                             {QStringLiteral("value"), QStringLiteral("New: A brand-new, unused item.")}}}}});
    QCOMPARE(parsed.specific(QStringLiteral("Condition")), QStringLiteral("New"));
}

void TestMatching::identityCase12()
{
    using Specifics = QList<ItemRecord::Specific>;
    auto identityOf = [this](const char *title, const Specifics &specifics) {
        return identify(item(QLatin1String(title), specifics), m_known, m_words);
    };
    // The owner's case: an unknown maker, "Unbranded/Generic" in the Brand
    // specific, a seller's code at the end, specs and an ASIN.
    const Specifics generic = {{QStringLiteral("Brand"), QStringLiteral("Unbranded/Generic")},
                               {QStringLiteral("MPN"), QStringLiteral("Does not Apply")},
                               {QStringLiteral("ASIN"), QStringLiteral("B0Q9ZZ12XY")},
                               {QStringLiteral("UPC"), QStringLiteral("Does not Apply")},
                               {QStringLiteral("Vertical sensitivity"), QStringLiteral("50 mV/div ~ 500 V/div")}};
    const Identity scope = identityOf("Zentrix 1014D 7\" LCD 2 Channel Signal Generator Digital Storage Oscilloscope H0V2", generic);
    QVERIFY2(scope.confident, qPrintable(scope.reason));
    QCOMPARE(scope.brandName, QStringLiteral("Zentrix"));
    QCOMPARE(scope.model, QStringLiteral("1014D"));
    QCOMPARE(buildQueries(scope), QStringList({"Zentrix 1014D"}));
    // notBrands values, alone or joined with "/", never fill the brand.
    for (const char *value : {"Unbranded", "Generic", "Does not apply", "N/A", "Unbranded/Generic", "Generic / Unbranded"}) {
        const Identity none = identityOf("scope 100MHz", {{QStringLiteral("Brand"), QLatin1String(value)}});
        QVERIFY2(none.brandName.isEmpty(), value);
    }
    // Title openers are no brand; numbers are no brand.
    QCOMPARE(identityOf("Vintage Zentrix 1014D scope", {}).brandName, QStringLiteral("Zentrix"));
    QCOMPARE(identityOf("New Zentrix QX9 scope", {}).model, QStringLiteral("QX9"));
    QVERIFY(identityOf("2 QX9 scope", {}).brandName.isEmpty());
    // A product code in the title is never the model.
    const Identity asin = identityOf("Zentrix B0Q9ZZ12XY 1014D scope", {{QStringLiteral("ASIN"), QStringLiteral("B0Q9ZZ12XY")}});
    QCOMPARE(asin.model, QStringLiteral("1014D"));
    // Two candidates, neither right after the brand nor a seller's code at
    // the end: ask, with the first one suggested (it pre-fills the prompt).
    const Identity ask = identityOf("Zentrix scope QX9 KQ7 tested", {{QStringLiteral("Brand"), QStringLiteral("Zentrix")}});
    QVERIFY(!ask.confident);
    QCOMPARE(ask.candidates.value(0), QStringLiteral("QX9"));
}

void TestMatching::searchNamesByEra()
{
    // Owner decision 2026-10-20: search names follow the model's era; every
    // name still matches result titles.
    const QByteArray json = R"({"format":1,"brands":[{"id":"oldco","names":[
        {"name":"OldCo","search":true,"searchBefore":1999},
        {"name":"Old Company","search":true,"searchBefore":1999},
        {"name":"MidCo","search":true,"searchBefore":2014,"onlyFor":"test equipment"},
        {"name":"NewCo","search":true,"onlyFor":"test equipment"}]}],
        "models":[
        {"brand":"oldco","model":"A100","category":{"name":"test equipment"},"year":{"first":1985}},
        {"brand":"oldco","model":"B200","category":{"name":"test equipment"},"year":{"first":2005}},
        {"brand":"oldco","model":"C300","category":{"name":"test equipment"},"year":{"first":2018}},
        {"brand":"oldco","model":"D400","category":{"name":"test equipment"}}]})";
    QString error;
    const std::optional<KnownModels> known = KnownModels::fromJson(json, &error);
    QVERIFY2(known, qPrintable(error));
    auto queriesOf = [&](const char *model) {
        return buildQueries(identityFromUser({QStringLiteral("OldCo"), QLatin1String(model), QString()}, *known, QStringLiteral("user")));
    };
    QCOMPARE(queriesOf("A100"), QStringList({"OldCo A100", "Old Company A100", "MidCo A100", "NewCo A100"}));
    QCOMPARE(queriesOf("B200"), QStringList({"MidCo B200", "NewCo B200"}));
    QCOMPARE(queriesOf("C300"), QStringList({"NewCo C300"}));
    // No year: every name (fallback).
    QCOMPARE(queriesOf("D400"), QStringList({"OldCo D400", "Old Company D400", "MidCo D400", "NewCo D400"}));
    // Matching keeps every name.
    const Identity newer = identityFromUser({QStringLiteral("OldCo"), QStringLiteral("C300"), QString()}, *known, QStringLiteral("user"));
    QCOMPARE(newer.matchNames, QStringList({"OldCo", "Old Company", "MidCo", "NewCo"}));
    // A searchBefore that is not a year is refused.
    QVERIFY(!KnownModels::fromJson(R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true,"searchBefore":"soon"}]}],"models":[]})", &error));
}

void TestMatching::ownListingIsNeverNotThisItem()
{
    // The pasted listing, also found among the for-sale rows.
    LookupResult result;
    result.item.itemId = QStringLiteral("100000000077");
    result.item.title = QStringLiteral("Acme QX200 digital scope");
    SearchRecord own;
    own.itemId = result.item.itemId;
    own.title = result.item.title;
    own.priceCents = 10000;
    own.shippingCents = 0;
    SearchRecord other = own;
    other.itemId = QStringLiteral("100000000078");
    result.active = {own, other};
    // An identity that does not match the listing's own title: wrong - no
    // row counted, the warning noted.
    result.identity = identityFromUser({QStringLiteral("Acme"), QStringLiteral("ZZ-B771"), QString()}, m_known, QStringLiteral("user"));
    result.identity.source = QStringLiteral("item specifics");
    QList<Row> rows = classifyRows(result, m_words);
    QVERIFY(!checkOwnListing(&result, &rows, m_words));
    QVERIFY(std::none_of(rows.cbegin(), rows.cend(), [](const Row &row) { return row.included; }));
    QVERIFY(result.notes.contains(QString::fromLatin1(kIdentityMismatch)));
    // The right identity: the listing is the same item, nothing noted.
    result.notes.clear();
    result.identity = identityFromUser({QStringLiteral("Acme"), QStringLiteral("QX200"), QString()}, m_known, QStringLiteral("title"));
    rows = classifyRows(result, m_words);
    QVERIFY(checkOwnListing(&result, &rows, m_words));
    QCOMPARE(rows.first().match.bucket, Bucket::SameItem);
    QVERIFY(result.notes.isEmpty());
    // An identity the user set: the listing's own row counts as the same
    // item even when its title lacks the code.
    result.identity = identityFromUser({QStringLiteral("Acme"), QStringLiteral("QX201"), QString()}, m_known, QStringLiteral("user"));
    rows = classifyRows(result, m_words);
    QVERIFY(checkOwnListing(&result, &rows, m_words));
    QCOMPARE(rows.first().match.bucket, Bucket::SameItem);
    QVERIFY(rows.first().included);
    QCOMPARE(rows.last().match.bucket, Bucket::NoMatch);
}

void TestMatching::rows_data()
{
    QTest::addColumn<QString>("brand");
    QTest::addColumn<QString>("model");
    QTest::addColumn<QString>("title");
    QTest::addColumn<QString>("ebayCondition"); // shown to the user, not used for matching
    QTest::addColumn<Bucket>("bucket");
    const QString heathkit = QStringLiteral("Heathkit");
    const QString sencore = QStringLiteral("Sencore");
    // Case 1, Heathkit IM-13.
    QTest::newRow("1 card #IM13") << heathkit << "IM-13" << "Trading card #IM13 rookie" << "" << Bucket::ToCheck;
    QTest::newRow("1 brake kit") << heathkit << "IM-13" << "9 inch 13\" disc brake kit" << "" << Bucket::NoMatch;
    QTest::newRow("1 other model") << heathkit << "IM-13" << "Heathkit IM-16 VTVM" << "" << Bucket::NoMatch;
    QTest::newRow("1 manual") << heathkit << "IM-13" << "Heathkit IM-13 Assembly Manual" << "" << Bucket::Documentation;
    QTest::newRow("1 part") << heathkit << "IM-13" << "Heathkit IM13, IM48 & IM9001 meter movement, P/N 100-234" << ""
                            << Bucket::PartFits;
    QTest::newRow("1 lot") << heathkit << "IM-13" << "LOT OF 3 Heathkit IM-13 meters" << "" << Bucket::Lot;
    QTest::newRow("1 broken") << heathkit << "IM-13" << "Heathkit IM-13 meter powered on / parts repair as is" << "Pre-Owned"
                              << Bucket::SameItem;
    QTest::newRow("1 with leads") << heathkit << "IM-13" << "Heathkit IM-13 analog voltmeter 0-1500V w/ Leads"
                                  << "Pre-Owned" << Bucket::SameItem;
    QTest::newRow("1 spaced, tested") << heathkit << "IM-13" << "Heathkit IM 13 vacuum tube voltmeter tested working" << ""
                                      << Bucket::SameItem;
    QTest::newRow("1 not working") << heathkit << "IM-13" << "Heathkit IM-13 not working" << "" << Bucket::SameItem;
    QTest::newRow("1 untested") << heathkit << "IM-13" << "Heathkit IM-13 VTVM untested" << "" << Bucket::SameItem;
    QTest::newRow("1 case for") << heathkit << "IM-13" << "Carrying Case for Heathkit IM-13" << "" << Bucket::Accessory;
    QTest::newRow("1 holster") << heathkit << "IM-13" << "Heathkit IM-13 holster" << "" << Bucket::Accessory;
    QTest::newRow("1 needs cord") << heathkit << "IM-13" << "Heathkit IM-13 voltmeter (needs a cord)" << "Pre-Owned"
                                  << Bucket::SameItem;
    QTest::newRow("1 eBay parts only") << heathkit << "IM-13" << "Heathkit IM-13 VTVM Vacuum Tube Voltmeter" << "Parts Only"
                                       << Bucket::SameItem;
    QTest::newRow("1 eBay new other") << heathkit << "IM-13" << "Heathkit IM-13 VTVM" << "New (Other)" << Bucket::SameItem;
    QTest::newRow("1 w/ glued") << heathkit << "IM-13" << "Heathkit IM-13 voltmeter w/Leads" << "" << Bucket::SameItem;
    QTest::newRow("1 not tested") << heathkit << "IM-13" << "Heathkit IM-13 not tested" << "" << Bucket::SameItem;
    QTest::newRow("1 in original case") << heathkit << "IM-13" << "Heathkit IM-13 voltmeter in original case" << ""
                                        << Bucket::SameItem;
    QTest::newRow("1 mains voltage, cord") << heathkit << "IM-13" << "Heathkit IM-13 VTVM 120VAC power cord" << ""
                                           << Bucket::SameItem;
    QTest::newRow("1 original parts") << heathkit << "IM-13" << "Heathkit IM-13 all original parts" << ""
                                      << Bucket::SameItem;
    QTest::newRow("1 no title") << heathkit << "IM-13" << "" << "" << Bucket::UnknownItem;
    QTest::newRow("6 decade and version") << heathkit << "AA-100" << "Heathkit AA-100 1960s Mk2 amplifier" << ""
                                          << Bucket::SameItem;
    QTest::newRow("6 restored, new caps") << heathkit << "AA-100" << "Heathkit AA-100 restored, new caps" << ""
                                          << Bucket::SameItem;
    // Case 2, Sencore LC53.
    QTest::newRow("2 other model") << sencore << "LC53" << "Sencore LC103 Z Meter" << "" << Bucket::NoMatch;
    QTest::newRow("2 longer model") << sencore << "LC53" << "Sencore LC530 tester" << "" << Bucket::NoMatch;
    QTest::newRow("2 with manual") << sencore << "LC53" << "Sencore LC53 Z Meter with manual and leads" << ""
                                   << Bucket::SameItem;
    QTest::newRow("2 hyphen") << sencore << "LC53" << "Sencore LC-53 Z Meter Capacitor Analyzer" << "Pre-Owned"
                              << Bucket::SameItem;
    // Case 3, Sencore PR570.
    QTest::newRow("3 inside a part number") << sencore << "PR570" << "Cargo mat Sencore QT-PR570-ZZ" << ""
                                            << Bucket::NoMatch;
    QTest::newRow("3 manual") << sencore << "PR570" << "Sencore Pr570 manual (PDF)" << "" << Bucket::Documentation;
    QTest::newRow("3 feet") << sencore << "PR570" << "2pcs Sencore PR570 compatible rubber foot/feet" << ""
                            << Bucket::PartFits;
    QTest::newRow("3 item") << sencore << "PR570" << "Sencore PR570 Powerite II isolation transformer" << "Pre-Owned"
                            << Bucket::SameItem;
    // Case 4, Sencore VG91.
    QTest::newRow("4 companion") << sencore << "VG91" << "Sencore QZ50 video amplifier made for VG91" << ""
                                 << Bucket::Companion;
    QTest::newRow("4 knobs") << sencore << "VG91" << "Pair Sencore knobs QA10, QB20, QC30, VG91, others" << ""
                             << Bucket::PartFits;
    QTest::newRow("4 cover several models") << sencore << "VG91" << "Sencore front cover QX12 for QA10 QB20 VG91"
                                            << "" << Bucket::PartFits;
    QTest::newRow("4 TV repair gear") << sencore << "VG91" << "Sencore VG91 video generator TV repair test equipment" << ""
                                      << Bucket::SameItem;
    // A lead set is an accessory, also as a "complete set"; a training tape is
    // documentation; another model joined by "and" makes two items (a lot).
    QTest::newRow("lead set complete") << sencore << "QG70" << "New Sencore QG70 test lead set (complete set of 6)" << ""
                                       << Bucket::Accessory;
    QTest::newRow("leads with the item") << sencore << "QG70" << "Sencore QG70 stereo analyzer with test leads"
                                         << "Pre-Owned" << Bucket::SameItem;
    QTest::newRow("lead acid is no lead") << sencore << "QG70" << "Sencore QG70 analyzer, new lead acid battery inside"
                                          << "Pre-Owned" << Bucket::SameItem;
    QTest::newRow("training tape") << sencore << "QG70" << "Sencore QG70 stereo analyzer troubleshooting tech tape" << ""
                                   << Bucket::Documentation;
    QTest::newRow("two items joined") << sencore << "QG70" << "Sencore QP61 power analyzer and QG70 stereo analyzer!!"
                                      << "For parts or not working" << Bucket::Lot;
    QTest::newRow("two items ampersand") << sencore << "QG70" << "Sencore QG70 & QP61 analyzers" << "" << Bucket::Lot;
    QTest::newRow("and inside the item") << sencore << "QG70" << "Sencore QG70 AM and FM stereo analyzer" << "Pre-Owned"
                                         << Bucket::SameItem;
    // Every word of the title counts (owner decision f1, 2026-10-21):
    // cables wherever they stand, unless a with-word puts them with the item;
    // "to connect" / "connects to" make a part. Not "cord" or "adapter" on
    // their own: owner cases 1 ("IM-13 VTVM 120VAC power cord") and the wave
    // guide adapter are the item.
    QTest::newRow("cable set") << sencore << "QG70" << "Sencore QG70 RF output cable set" << "" << Bucket::Accessory;
    QTest::newRow("cable with the item") << sencore << "QG70" << "Sencore QG70 stereo analyzer with RF cable"
                                         << "Pre-Owned" << Bucket::SameItem;
    QTest::newRow("connects to") << sencore << "QG70" << "Sencore probe tip connects to QG70 input" << ""
                                 << Bucket::PartFits;
    QTest::newRow("4 shoe code") << sencore << "VG91" << "Running shoe VG91-5040" << "" << Bucket::NoMatch;
    QTest::newRow("4 card grade") << sencore << "VG91" << "Baseball card #91 VG" << "" << Bucket::NoMatch;
    // Case 5, Sencore PR57.
    QTest::newRow("5 longer model") << sencore << "PR57" << "Sencore PR570 Powerite II" << "" << Bucket::NoMatch;
    QTest::newRow("5 holster") << sencore << "PR57" << "Acme PR57 holster" << "" << Bucket::ToCheck;
    QTest::newRow("5 stamp") << sencore << "PR57" << "#PR57 newspaper stamp" << "" << Bucket::ToCheck;
    QTest::newRow("5 new in box") << sencore << "PR57" << "Sencore PR57 Powerite new in box never opened" << "Pre-Owned"
                                  << Bucket::SameItem;
    // Case 6, Heathkit AA-100.
    QTest::newRow("6 car part") << heathkit << "AA-100" << "Car part 15208-AA100 filter" << "" << Bucket::NoMatch;
    QTest::newRow("6 roached") << heathkit << "AA-100" << "Heathkit AA-100 stereo amplifier completely roached, needs restoration"
                               << "" << Bucket::SameItem;
    QTest::newRow("6 NOS spaced") << heathkit << "AA-100" << "Heathkit AA 100 tube amp NOS never used" << ""
                                  << Bucket::SameItem;
    // Case 7, HP 8904A.
    QTest::newRow("7 item") << "HP" << "8904A" << "HP 8904A Multifunction Synthesizer DC 600 kHz" << "Pre-Owned"
                            << Bucket::SameItem;
    QTest::newRow("7 manual") << "HP" << "8904A" << "Hewlett Packard 8904A Operation and Calibration Manual" << ""
                              << Bucket::Documentation;
    QTest::newRow("7 successor names") << "HP" << "8904A" << "HP Agilent Keysight 8904A synthesizer" << ""
                                       << Bucket::SameItem;
    QTest::newRow("7 slash-joined brands") << "HP" << "8904A" << "HP/Agilent 8904A synthesizer" << "" << Bucket::SameItem;
    QTest::newRow("7 no brand") << "HP" << "8904A" << "8904a multifunction synthesizer dc-600" << "" << Bucket::ToCheck;
    QTest::newRow("7 successor only") << "HP" << "8904A" << "Agilent 8904a synthesizer" << "" << Bucket::SameItem;
    // Case 8, Marconi 2022 and its family.
    QTest::newRow("8 family member") << "Marconi" << "2022" << "Marconi 2022D signal generator" << ""
                                     << Bucket::SameItem;
    QTest::newRow("8 successor name") << "Marconi" << "2022" << "IFR 2022E signal generator" << "" << Bucket::SameItem;
    QTest::newRow("8 other model") << "Marconi" << "2022" << "Marconi 2023 signal generator" << "" << Bucket::NoMatch;
    QTest::newRow("8 digits away from brand") << "Marconi" << "2022" << "Marconi signal generator calibrated 2022" << ""
                                              << Bucket::ToCheck;
    QTest::newRow("8 letters-only model away from brand") << "Commodore" << "Amiga" << "Amiga computer made by Commodore"
                                                          << "" << Bucket::SameItem;
    QTest::newRow("8 split code away from brand") << "Acme" << "2022D" << "Acme signal generator 2022 D" << ""
                                                 << Bucket::SameItem;
    // Owner decision 2026-10-19: digits-only models.
    QTest::newRow("R5 right after brand") << "Racal-Dana" << "1992" << "Racal-Dana 1992 counter" << "" << Bucket::SameItem;
    QTest::newRow("R5 a year") << "Racal-Dana" << "1992" << "1992 Ford" << "" << Bucket::NoMatch;
    QTest::newRow("R5 manual") << "Racal-Dana" << "1992" << "Racal Dana counter 1992 manual" << "" << Bucket::Documentation;
    QTest::newRow("R5 EICO") << "EICO" << "625" << "EICO 625 tube tester" << "" << Bucket::SameItem;
    QTest::newRow("R5 Marconi") << "Marconi" << "2305" << "Marconi 2305 modulation meter" << "" << Bucket::SameItem;
    QTest::newRow("R5 slash brand") << "Marconi" << "2022" << "Marconi/IFR 2022 signal generator" << "" << Bucket::SameItem;
    QTest::newRow("R5 after product name") << "Marconi" << "2022" << "Marconi AM/FM Signal Generator 2022" << ""
                                           << Bucket::SameItem;
    // A longer known model of the brand is another model; a misspelt brand
    // still matches.
    QTest::newRow("longer model") << "Nintendo" << "Game Boy" << "Nintendo Game Boy Color console" << "" << Bucket::NoMatch;
    QTest::newRow("inside a longer model") << "Nintendo" << "NES" << "Nintendo Super NES console" << "" << Bucket::NoMatch;
    QTest::newRow("same machine, other code") << "Commodore" << "64" << "Commodore C64 computer" << "" << Bucket::SameItem;
    QTest::newRow("shorter model") << "Nintendo" << "Game Boy" << "Nintendo Game Boy DMG-01 handheld" << "" << Bucket::SameItem;
    QTest::newRow("misspelt brand") << "Sencore" << "LC102" << "Sencor LC102 Auto-Z analyzer" << "" << Bucket::SameItem;
    QTest::newRow("8 untested") << "Marconi" << "2022" << "Marconi Instruments 2022 AM/FM signal generator untested" << ""
                                << Bucket::SameItem;
    QTest::newRow("8 new by eBay") << "Marconi" << "2022" << "Marconi 2022 signal generator" << "Brand New"
                                   << Bucket::SameItem;
    // Case 9, Heathkit IP-20: repair and parts kits are parts; the brand's
    // own unbuilt kit is the item; any other kit is unsure.
    const QString ip20 = QStringLiteral("IP-20");
    QTest::newRow("9 recap kit") << heathkit << ip20
                                 << "Heathkit IP-20 RECAP KIT for Power Supply w/ Instructions - capacitors & diodes"
                                 << "Brand New" << Bucket::PartFits;
    QTest::newRow("9 cap kit") << heathkit << ip20 << "Heathkit IP-20 cap kit" << "" << Bucket::PartFits;
    QTest::newRow("9 kit for") << heathkit << ip20 << "Heathkit IP-20 kit for the regulated supply" << ""
                               << Bucket::PartFits;
    QTest::newRow("9 parts set") << heathkit << ip20 << "Heathkit IP-20 capacitors & diodes set" << ""
                                 << Bucket::PartFits;
    QTest::newRow("9 unbuilt kit") << heathkit << ip20 << "Heathkit IP-20 kit unbuilt" << "" << Bucket::SameItem;
    QTest::newRow("9 unsure kit") << heathkit << ip20 << "Heathkit IP-20 power supply kit" << "" << Bucket::ToCheck;
    QTest::newRow("9 the device") << heathkit << ip20 << "Heathkit IP-20 regulated power supply" << "Used"
                                  << Bucket::SameItem;
    // Brand-scan cases.
    QTest::newRow("S1 specs") << "Acme" << "M7X" << "Acme M7X 2-CHANNEL 4CH 2-PORT amplifier 10-WATT" << ""
                              << Bucket::SameItem;
    QTest::newRow("S2 options") << "HP" << "8904A" << "HP 8904A synthesizer Opt 1E5 1EA" << "" << Bucket::SameItem;
    QTest::newRow("S3 tubes") << heathkit << "AA-100" << "Heathkit AA-100 amplifier 12AX7 EL84" << "" << Bucket::SameItem;
    QTest::newRow("S4 seller code") << sencore << "LC53" << "Sencore LC53 Z meter QA10 QX7731" << "" << Bucket::SameItem;
    QTest::newRow("S5 supply") << "HP" << "E3634A" << "HP E3634A DC power supply 0-25V/7A 0-50V/4A" << ""
                               << Bucket::SameItem;
    QTest::newRow("S5 generator") << "HP" << "33511B" << "HP 33511B waveform generator 20MHz 250MSa/s 16BIT" << ""
                                  << Bucket::SameItem;
    QTest::newRow("S6 front panel") << sencore << "LC53" << "Sencore LC53 front panel" << "" << Bucket::PartFits;
    QTest::newRow("S6 crystal") << sencore << "LC53" << "Sencore LC53 crystal" << "" << Bucket::PartFits;
    QTest::newRow("S6 dial glass") << heathkit << "SB-301" << "Heathkit SB-301 dial glass" << "" << Bucket::PartFits;
    QTest::newRow("S6 knob") << heathkit << "SB-301" << "Heathkit SB-301 PARTS - knob" << "" << Bucket::PartFits;
    QTest::newRow("S6 board") << heathkit << "IM-13" << "Heathkit IM-13 board" << "" << Bucket::PartFits;
    QTest::newRow("S6 with front panel") << sencore << "LC53" << "Sencore LC53 Z meter with original front panel" << ""
                                         << Bucket::SameItem;
    QTest::newRow("S7 advertisement") << heathkit << "IM-13" << "Heathkit IM-13 advertisement" << ""
                                      << Bucket::Documentation;
    QTest::newRow("S7 data book") << sencore << "LC53" << "Sencore tube data book LC53" << "" << Bucket::Documentation;
    QTest::newRow("S7 catalog") << heathkit << "IM-13" << "Heathkit catalog IM-13" << "" << Bucket::Documentation;
    QTest::newRow("S8 Heath Kit") << heathkit << "IM-13" << "Heath Kit IM-13 VTVM" << "" << Bucket::SameItem;
    QTest::newRow("S10 with tube set") << heathkit << "SB-301" << "Heathkit SB-301 receiver with NOS tube set" << ""
                                       << Bucket::SameItem;
    QTest::newRow("S10 tube kit") << heathkit << "SB-301" << "Heathkit SB-301 tube kit" << "" << Bucket::ToCheck;
    QTest::newRow("S11 feet set") << sencore << "LC53" << "Sencore LC53 compatible rubber feet set" << ""
                                  << Bucket::PartFits;
    QTest::newRow("S11 bumper kit") << sencore << "LC53" << "Sencore LC53 bumper handle kit" << "" << Bucket::PartFits;
    QTest::newRow("S12 original case") << heathkit << "IM-13" << "Heathkit IM-13 VTVM original case" << ""
                                       << Bucket::SameItem;
    QTest::newRow("S12 wood case") << heathkit << "IM-13" << "Heathkit IM-13 wood case" << "" << Bucket::SameItem;
    QTest::newRow("S12 carrying case") << heathkit << "IM-13" << "Heathkit IM-13 carrying case" << ""
                                       << Bucket::Accessory;
    QTest::newRow("S16 station package") << heathkit << "IM-13" << "Heathkit IM-13 station package" << "" << Bucket::Lot;
    QTest::newRow("S17 after brand") << "Acme" << "2445" << "Acme 2445 oscilloscope" << "" << Bucket::SameItem;
    QTest::newRow("S17 after model word") << "Acme" << "2445" << "Acme model 2445 oscilloscope" << ""
                                          << Bucket::SameItem;
    QTest::newRow("S17 away from brand") << "Acme" << "2445" << "Acme oscilloscope 2445" << "" << Bucket::ToCheck;
    QTest::newRow("S18 split code") << sencore << "TC154" << "Sencore TC 154 tube checker" << "" << Bucket::SameItem;
    // Retro-scan cases.
    const QString atari = QStringLiteral("Atari");
    QTest::newRow("R1 cartridge") << atari << "2600" << "Moon Raid Atari 2600 game cartridge" << "" << Bucket::Companion;
    QTest::newRow("R1 CIB") << atari << "2600" << "Star Hopper Atari 2600 CIB with manual" << "" << Bucket::Companion;
    QTest::newRow("R1 console with games") << atari << "2600" << "Atari 2600 console with 5 games" << ""
                                           << Bucket::SameItem;
    QTest::newRow("R1 product name") << atari << "2600" << "Atari 2600 Video Computer System" << "" << Bucket::SameItem;
    QTest::newRow("R2 controllers") << atari << "2600" << "Two Atari 2600 paddle controllers, tested" << ""
                                    << Bucket::Accessory;
    QTest::newRow("R2 power supply") << atari << "2600" << "Wall power supply, Atari 2600" << "" << Bucket::Accessory;
    QTest::newRow("R2 console with controllers") << atari << "2600" << "Atari 2600 console with 2 controllers" << ""
                                                 << Bucket::SameItem;
    QTest::newRow("R3 magazine") << atari << "ST" << "Atari ST users magazine number 7 with cover disk" << ""
                                 << Bucket::Documentation;
    QTest::newRow("R4 classic edition") << "Nintendo" << "NES" << "Nintendo NES Classic Edition" << "" << Bucket::NoMatch;
    QTest::newRow("R4 flashback") << atari << "7800" << "Atari 7800 Flashback" << "" << Bucket::NoMatch;
    QTest::newRow("R4 mini") << "Commodore" << "64" << "Commodore C64 Mini boxed" << "" << Bucket::NoMatch;
    QTest::newRow("R4 not next to the model") << atari << "2600" << "Atari 2600 console with HDMI mod" << ""
                                              << Bucket::SameItem;
    QTest::newRow("R5 size and video specs") << "Nintendo" << "Switch" << "Nintendo Switch 64GB 1080P console" << ""
                                             << Bucket::SameItem;
    QTest::newRow("R5 Spectrum 48K") << "Sinclair" << "ZX Spectrum" << "Sinclair ZX Spectrum 48K computer" << ""
                                     << Bucket::SameItem;
    QTest::newRow("R6 other system") << atari << "2600" << "Atari 2600 Commodore 64 dual interface" << ""
                                     << Bucket::Accessory;
    QTest::newRow("R7 other maker") << atari << "Lynx" << "Camputers Lynx home computer, boxed" << "" << Bucket::NoMatch;
    QTest::newRow("R7 Enterprise") << "Commodore" << "64" << "Enterprise 64 home computer" << "" << Bucket::NoMatch;
    QTest::newRow("R8 inside a part number") << "IBM" << "1130" << "IBM ThinkPad keyboard 77Z1130" << ""
                                             << Bucket::NoMatch;
    QTest::newRow("R8 CPU") << "HP" << "2100" << "Intel Core i3-2100 CPU" << "" << Bucket::NoMatch;
    QTest::newRow("R9 connectors") << "Commodore" << "64" << "Commodore 64 computer DB9 RS232 5FT 14-PIN" << ""
                                   << Bucket::SameItem;
    QTest::newRow("R10 chip") << "Commodore" << "64" << "Commodore 64 SID 8580R5" << "" << Bucket::PartFits;
    QTest::newRow("R10 with the computer") << "Commodore" << "64" << "Commodore 64 computer 8580R5 SID" << ""
                                           << Bucket::SameItem;
    QTest::newRow("R10 board") << "Commodore" << "64" << "Commodore 64 board REV.6A" << "" << Bucket::PartFits;
    QTest::newRow("R11 bundle") << atari << "2600" << "Atari 2600 bundle 10 games 2 joysticks" << "" << Bucket::SameItem;
    QTest::newRow("R12 box only") << atari << "2600" << "Atari 2600 box only" << "" << Bucket::PartFits;
    QTest::newRow("R12 poster") << atari << "2600" << "Atari 2600 poster" << "" << Bucket::NoMatch;
    QTest::newRow("R12 shirt") << atari << "2600" << "Atari 2600 t-shirt" << "" << Bucket::NoMatch;
    QTest::newRow("R13 game in the model") << "Nintendo" << "Game Boy" << "Nintendo Game Boy handheld" << ""
                                           << Bucket::SameItem;
    QTest::newRow("R13 game cartridge") << "Nintendo" << "Game Boy" << "Nintendo Game Boy Block Drop game cartridge" << ""
                                        << Bucket::Companion;
    // Words that must not misjudge a real item (review of 2026-10-20).
    QTest::newRow("no merchandise in test gear") << "Tektronix" << "2445" << "Tektronix 2445 with BNC tee and patch cords"
                                                 << "" << Bucket::SameItem;
    QTest::newRow("chip with the computer") << "Commodore" << "64" << "Commodore 64 computer, new SID chip" << ""
                                            << Bucket::SameItem;
    QTest::newRow("wave guide") << "HP" << "X281A" << "HP X281A wave guide adapter" << "" << Bucket::SameItem;
    // Oscilloscopes: mainframes and plug-ins, probes, keys, specs.
    const QString tek = QStringLiteral("Tektronix");
    const QString rigol = QStringLiteral("Rigol");
    QTest::newRow("scope mainframe with plug-ins") << tek << "7603" << "Tektronix 7603 oscilloscope mainframe with 7A26 and 7B53A plug-ins"
                                                   << "" << Bucket::SameItem;
    // A digits-only model away from the brand is "to check" (owner decision
    // 2026-10-19), never counted.
    QTest::newRow("scope plug-in for the mainframe") << tek << "7603" << "Tektronix 7A26 plug-in for 7603 mainframe" << ""
                                                     << Bucket::ToCheck;
    QTest::newRow("scope the plug-in itself") << tek << "7A26" << "Tektronix 7A26 plug-in for 7603 mainframe" << ""
                                              << Bucket::SameItem;
    QTest::newRow("scope license key") << rigol << "DS1054Z" << "Rigol DS1054Z license key unlock all options" << ""
                                       << Bucket::NoMatch;
    QTest::newRow("scope firmware update") << rigol << "DS1054Z" << "Rigol DS1054Z firmware update service" << ""
                                           << Bucket::NoMatch;
    QTest::newRow("scope specs") << rigol << "DS1054Z" << "Rigol DS1054Z 50MHz unlocked 100MHz 1GSa/s 4CH" << ""
                                 << Bucket::SameItem;
    QTest::newRow("scope probe set") << rigol << "DS1054Z" << "Rigol DS1054Z probe set" << "" << Bucket::Accessory;
    QTest::newRow("scope probes for it") << rigol << "DS1054Z" << "Rigol PVP2350 probe set for DS1054Z" << ""
                                         << Bucket::Companion;
    QTest::newRow("scope accessory for Tektronix") << tek << "2445" << "Probe for Tektronix 2445" << "" << Bucket::Accessory;
}

void TestMatching::rows()
{
    QFETCH(QString, brand);
    QFETCH(QString, model);
    QFETCH(QString, title);
    QFETCH(QString, ebayCondition);
    QFETCH(Bucket, bucket);
    const Identity identity = identityFromUser({brand, model, QString()}, m_known, QStringLiteral("user"));
    QVERIFY(identity.confident);
    Q_UNUSED(ebayCondition)
    const Match match = matchRow(title, identity, m_words);
    QVERIFY2(match.bucket == bucket, qPrintable(bucketName(match.bucket) + QStringLiteral(": ") + match.reasons.join(QStringLiteral("; "))));
    QVERIFY(!match.reasons.isEmpty());
}

void TestMatching::decisionPath()
{
    // The reasons say how the row was decided (owner decision f1).
    const Identity identity = identityFromUser({QStringLiteral("Sencore"), QStringLiteral("QG70"), QString()}, m_known,
                                               QStringLiteral("user"));
    const Match leads = matchRow(QStringLiteral("Sencore QG70 test leads"), identity, m_words);
    QCOMPARE(leads.bucket, Bucket::Accessory);
    QVERIFY2(leads.reasons.last().contains(QStringLiteral("\"leads\" = accessory word, not after a with-word")),
             qPrintable(leads.reasons.join(QStringLiteral("; "))));
    const Match withLeads = matchRow(QStringLiteral("Sencore QG70 stereo analyzer with leads"), identity, m_words);
    QCOMPARE(withLeads.bucket, Bucket::SameItem);
    QVERIFY2(withLeads.reasons.join(QStringLiteral("; ")).contains(QStringLiteral("\"leads\" after \"with\": comes with the item")),
             qPrintable(withLeads.reasons.join(QStringLiteral("; "))));
    QVERIFY(withLeads.reasons.last().endsWith(QStringLiteral("-> same item")));
}

void TestMatching::descriptionFindings()
{
    // The seller's description (owner decision f2/f3): only clear phrases
    // move a row; ordinary item text does not.
    const Identity identity = identityFromUser({QStringLiteral("Sencore"), QStringLiteral("QG70"), QString()}, m_known,
                                               QStringLiteral("user"));
    auto finding = [&](const char *text) { return descriptionFinding(QString::fromLatin1(text), identity, m_words); };
    QCOMPARE(finding("New in bag. Leads only, the analyzer is not part of this sale.").bucket, Bucket::Accessory);
    QVERIFY(finding("Leads only.").reason.contains(QStringLiteral("\"leads only\"")));
    QCOMPARE(finding("Manual only, 40 pages.").bucket, Bucket::Documentation);
    QCOMPARE(finding("You get the case and leads. Unit not included.").bucket, Bucket::Accessory);
    QCOMPARE(finding("The QG70 is not included, sorry.").bucket, Bucket::Accessory);
    QCOMPARE(finding("A job lot from an estate.").bucket, Bucket::Lot);
    QCOMPARE(finding("Sold for parts, it does not power up.").bucket, Bucket::PartFits);
    QCOMPARE(finding("Not working, no display.").bucket, Bucket::PartFits);
    // Untested: a note, the row stays.
    const DescriptionFinding untested = finding("Powers on, I have no way of testing it. Sold as-is.");
    QCOMPARE(untested.bucket, Bucket::SameItem);
    QVERIFY(untested.note.contains(QStringLiteral("no way of testing")) || untested.note.contains(QStringLiteral("as-is")));
    // Ordinary text about the item itself: nothing.
    for (const char *plain : {"Works great, comes with leads and the manual.", "Only the case is scratched.",
                              "Tested on 120V, all functions work.", "The leads are included with the unit.", ""}) {
        const DescriptionFinding nothing = finding(plain);
        QVERIFY2(nothing.bucket == Bucket::SameItem && nothing.note.isEmpty(), plain);
    }
}

void TestMatching::productNameWords()
{
    // S9: the product name holds a kit word; S13: the item is a probe.
    Identity testSet = identityFromUser({QStringLiteral("HP"), QStringLiteral("8920A"), QString()}, m_known,
                                        QStringLiteral("user"));
    QCOMPARE(matchRow(QStringLiteral("HP 8920A RF Communications Test Set"), testSet, m_words).bucket, Bucket::ToCheck);
    testSet.productName = QStringLiteral("RF Communications Test Set");
    QCOMPARE(matchRow(QStringLiteral("HP 8920A RF Communications Test Set"), testSet, m_words).bucket, Bucket::SameItem);

    // The known N2894A is a "Passive Probe": its own name is no accessory.
    const Identity probe = identityFromUser({QStringLiteral("HP"), QStringLiteral("N2894A"), QString()}, m_known,
                                            QStringLiteral("user"));
    QCOMPARE(probe.productName, QStringLiteral("Passive Probe"));
    QCOMPARE(matchRow(QStringLiteral("Agilent N2894A passive probe 700MHz"), probe, m_words).bucket, Bucket::SameItem);
    QCOMPARE(matchRow(QStringLiteral("Keysight N2894A probe"), probe, m_words).bucket, Bucket::SameItem);
    // For any other item a probe stays an accessory.
    const Identity synthesizer = identityFromUser({QStringLiteral("HP"), QStringLiteral("8904A"), QString()}, m_known,
                                                  QStringLiteral("user"));
    QCOMPARE(matchRow(QStringLiteral("HP 8904A probe"), synthesizer, m_words).bucket, Bucket::Accessory);
}

void TestMatching::valueOverAllRows()
{
    // One set of numbers over every same-item sale, whatever its condition
    // (owner decision 2026-10-19).
    const app::ValueSettings settings;
    QList<Comp> comps;
    for (int i = 0; i < 10; ++i)
        comps.append({10000 + i * 1000, 1000, true});
    comps.append({200000, 1000, true});        // outlier
    comps.append({50000, 1000, false});        // unconfirmed: apart
    comps.append({std::nullopt, std::nullopt, true}); // no total: not counted
    const ValueNumbers numbers = valueOf(comps, settings);
    QCOMPARE(numbers.included, 10);
    QCOMPARE(numbers.outliers, QList<qint64>({200000}));
    QCOMPARE(numbers.medianCents, std::optional<qint64>(14500));
    QCOMPARE(numbers.p25Cents, std::optional<qint64>(12250));
    QCOMPARE(numbers.p75Cents, std::optional<qint64>(16750));
    QCOMPARE(numbers.confidence, QStringLiteral("High"));
    QCOMPARE(numbers.unconfirmed, 1);
    QCOMPARE(numbers.unconfirmedMedianCents, std::optional<qint64>(50000));
    QCOMPARE(valueOf({{2000, 500, true}, {3000, 500, true}}, settings).confidence, QStringLiteral("Low"));
    QCOMPARE(valueOf({}, settings).medianCents, std::nullopt);
    QCOMPARE(confidenceName(1, settings), QStringLiteral("Not enough data"));
    QCOMPARE(confidenceName(5, settings), QStringLiteral("Medium"));
}

// Case 9: the $32.00 recap kit was ticked and marked CHEAPEST. As a part it
// is not counted, so "for sale now, from" and the cheapest marks (both from
// the counted for-sale rows) start at the real item.
void TestMatching::partsKitIsNotCheapest()
{
    LookupResult result;
    result.status = QStringLiteral("complete");
    result.item.itemId = QStringLiteral("100000000099");
    result.item.priceCents = 9000;
    result.item.shippingCents = 2000;
    result.identity = identityFromUser({QStringLiteral("Heathkit"), QStringLiteral("IP-20"), QString()}, m_known,
                                       QStringLiteral("user"));
    auto forSale = [](const char *id, const char *title, qint64 price, qint64 shipping) {
        SearchRecord record;
        record.itemId = QString::fromLatin1(id);
        record.title = QString::fromLatin1(title);
        record.priceCents = price;
        record.shippingCents = shipping;
        record.format = QStringLiteral("fixed_price");
        return record;
    };
    result.active = {forSale("200000000091", "Heathkit IP-20 RECAP KIT for Power Supply w/ Instructions - capacitors & diodes",
                             3200, 0),
                     forSale("200000000092", "Heathkit IP-20 regulated power supply", 8000, 1500),
                     forSale("200000000093", "Heathkit IP-20 power supply tested", 12000, 0)};
    // Owner decision 2026-10-19: only Buy It Now listings count as "for sale
    // now" prices; a running auction's $20.00 bid is shown apart; a card
    // without a format counts in neither.
    SearchRecord auction = forSale("200000000094", "Heathkit IP-20 power supply", 2000, 1000);
    auction.format = QStringLiteral("auction");
    auction.bids = 2;
    SearchRecord unknown = forSale("200000000095", "Heathkit IP-20 supply", 5000, 0);
    unknown.format.clear();
    result.active.append(auction);
    result.active.append(unknown);
    const QList<Row> rows = classifyRows(result, m_words);
    QCOMPARE(rows.size(), 5);
    QCOMPARE(rows.at(0).match.bucket, Bucket::PartFits);
    QCOMPARE(rows.at(0).included, false);
    const Valuation valuation = valuate(result, rows, app::ValueSettings());
    QCOMPARE(valuation.forSale, QList<qint64>({9500, 12000}));
    QCOMPARE(valuation.cheaperForSale, 1);
    QCOMPARE(valuation.auctions, QList<qint64>({2000}));
}

// Case 10: "Brand: Heath Zenith" is Heathkit; IP-17, IP-2717, IP-2717A,
// SP-2717, SP-2717A are one item and each is searched; "Zenith" alone is
// its own brand; PS-4 and IP-32 are other items.
void TestMatching::heathZenithFamily()
{
    ItemRecord item;
    item.itemId = QStringLiteral("100000000097");
    item.title = QStringLiteral("Heathkit Zenith SP-2717A High Voltage Power Supply Military Radio & Tube-Tested");
    item.specifics = {{QStringLiteral("Brand"), QStringLiteral("Heath Zenith")}, {QStringLiteral("Model"), QStringLiteral("IP-2717A")}};
    const Identity identity = identify(item, m_known, m_words);
    QVERIFY2(identity.confident, qPrintable(identity.reason));
    QCOMPARE(identity.brandId, QStringLiteral("heathkit"));
    QCOMPARE(identity.model, QStringLiteral("IP-2717A"));
    QCOMPARE(buildQueries(identity), QStringList({"Heathkit IP-2717A", "Heathkit IP-17", "Heathkit IP-2717", "Heathkit SP-2717",
                                                  "Heathkit SP-2717A"}));
    for (const char *title : {"Heath Zenith SP-2717 high voltage power supply", "Heathkit IP-17 high voltage supply",
                              "Heath IP-2717A power supply", "Heath/Zenith IP-2717 supply"}) {
        const Match match = matchRow(QString::fromLatin1(title), identity, m_words);
        QVERIFY2(match.bucket == Bucket::SameItem, title);
    }
    for (const char *title : {"Heathkit PS-4 regulated power supply", "Heathkit IP-32 power supply"})
        QCOMPARE(matchRow(QString::fromLatin1(title), identity, m_words).bucket, Bucket::NoMatch);

    ItemRecord zenith;
    zenith.itemId = QStringLiteral("100000000098");
    zenith.title = QStringLiteral("Zenith Trans-Oceanic H500 radio");
    zenith.specifics = {{QStringLiteral("Brand"), QStringLiteral("Zenith")}, {QStringLiteral("Model"), QStringLiteral("H500")}};
    const Identity radio = identify(zenith, m_known, m_words);
    QVERIFY(radio.brandId != QStringLiteral("heathkit"));
    QCOMPARE(radio.brandName, QStringLiteral("Zenith"));
}

// Owner decision 2026-10-19: a red "likely broken" warning from eBay's parts
// condition or the owner's words; nothing else is guessed.
void TestMatching::likelyBroken()
{
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM"), QStringLiteral("For parts or not working"), m_words),
             QStringLiteral("eBay says 'For parts or not working'"));
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM"), QStringLiteral("Parts Only"), m_words),
             QStringLiteral("eBay says 'Parts Only'"));
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM Powers On - Untested"), QStringLiteral("Used"), m_words),
             QStringLiteral("the title says 'untested'"));
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM parts/repair"), QStringLiteral("Used"), m_words),
             QStringLiteral("the title says 'parts/repair'"));
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM sold AS-IS"), QString(), m_words),
             QStringLiteral("the title says 'as-is'"));
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM Tested Working"), QStringLiteral("Used"), m_words), QString());
    QCOMPARE(likelyBrokenReason(QStringLiteral("Heathkit IM-13 VTVM"), QStringLiteral("Pre-Owned"), m_words), QString());
}

void TestMatching::shippingCheck()
{
    const app::ValueSettings settings;
    QList<Comp> comps;
    for (const qint64 shipping : {800, 931, 1000})
        comps.append({10000, shipping, true});
    // Case 4: $122.80 against a median of $9.31 -> flagged.
    const ShippingCheck high = checkShipping(12280, comps, 931, settings);
    QCOMPARE(high.compMedianCents, std::optional<qint64>(931));
    QCOMPARE(high.ebayAverageCents, std::optional<qint64>(931));
    QVERIFY(high.flagged);
    // A heavy item: $58 against a median of $40 of its comps -> not flagged.
    QList<Comp> heavy;
    for (const qint64 shipping : {3500, 4000, 4500})
        heavy.append({50000, shipping, true});
    QVERIFY(!checkShipping(5800, heavy, std::nullopt, settings).flagged);
    // Unconfirmed sales do not count for the median shipping.
    heavy.append({50000, 100, false});
    QCOMPARE(checkShipping(5800, heavy, std::nullopt, settings).compsWithShipping, 3);
}

QTEST_GUILESS_MAIN(TestMatching)
#include "tst_matching.moc"
