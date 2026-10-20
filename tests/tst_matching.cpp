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
    void dataFilesNameTheirSources();
    void userFileSavesAndReloads();
    void userFileIsNotTrustedBlindly();
    void titleWords();
    void specifications_data();
    void specifications();
    void identityPerCase();
    void identityWithoutKnownBrand();
    void rows_data();
    void rows();
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

void TestMatching::dataFilesNameTheirSources()
{
    // The shipped data reads with every source required (builtIn did).
    QCOMPARE(m_known.brands().size(), 80);
    QVERIFY(m_known.findModel(QStringLiteral("marconi"), QStringLiteral("2022D")));
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
    // A fact without a source is refused in shipped data, accepted in the
    // user's file.
    const QByteArray noSource = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true}]}],"models":[]})";
    QString error;
    QVERIFY(!KnownModels::fromJson(noSource, true, &error));
    QVERIFY2(error.contains(QStringLiteral("no source")), qPrintable(error));
    QVERIFY(KnownModels::fromJson(noSource, false, &error));
    const QByteArray badYear = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true,"source":"owner"}]}],
        "models":[{"brand":"x","model":"M7X","source":"owner","year":{"first":1990,"last":1980,"source":"owner"}}]})";
    QVERIFY(!KnownModels::fromJson(badYear, true, &error));
    QVERIFY2(error.contains(QStringLiteral("year")), qPrintable(error));
    const QByteArray yearNoSource = R"({"format":1,"brands":[{"id":"x","names":[{"name":"X","search":true,"source":"owner"}]}],
        "models":[{"brand":"x","model":"M7X","source":"owner","year":{"first":1990}}]})";
    QVERIFY(!KnownModels::fromJson(yearNoSource, true, &error));
    QVERIFY(KnownModels::fromJson(yearNoSource, false, &error));
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

    // The item specifics name the model.
    Identity bySpecifics = identify(item(QStringLiteral("Vintage Sencore analyzer"),
                                         {{QStringLiteral("Brand"), QStringLiteral("Sencore")},
                                          {QStringLiteral("Model"), QStringLiteral("LC53")}}),
                                    m_known, m_words);
    QCOMPARE(bySpecifics.model, QStringLiteral("LC53"));
    QVERIFY(bySpecifics.confident);
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
    // Two model-like words: ask.
    const Identity two = identify(item(QStringLiteral("Acme M7X B2 meter"), {{QStringLiteral("Brand"), QStringLiteral("Acme")}}),
                                  m_known, m_words);
    QVERIFY(!two.confident);
    QCOMPARE(two.candidates, QStringList({"M7X", "B2"}));
    // No brand at all: ask, even with a model word.
    const Identity none = identify(item(QStringLiteral("M7X bench meter")), m_known, m_words);
    QVERIFY(!none.confident);
    QVERIFY2(none.reason.contains(QStringLiteral("no brand")), qPrintable(none.reason));
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
    QTest::newRow("R5 after product name") << "Marconi" << "2022" << "Marconi AM/FM Signal Generator 2022" << ""
                                           << Bucket::SameItem;
    // A longer known model of the brand is another model; a misspelt brand
    // still matches.
    QTest::newRow("longer model") << "Nintendo" << "Game Boy" << "Nintendo Game Boy Color console" << "" << Bucket::NoMatch;
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
