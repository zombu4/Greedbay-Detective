#include "DevWindow.h"

#include "browser/BrowserController.h"
#include "core/BrowserProfiles.h"
#include "core/PageBudget.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "ebay/SearchUrls.h"
#include "lookup/LookupRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <utility>

namespace {

// One active search for a common item, 240 results per page.
const char kTestUrl[] = "https://www.ebay.com/sch/i.html?_nkw=usb+c+cable&_ipg=240";

// The spin boxes need a maximum; the setting itself has none (the daily page
// cap limits the loads).
constexpr int kDepthSpinMaximum = 100000;

enum Column { ColUse, ColSource, ColTotal, ColCondition, ColExtra, ColTitle, ColReasons, ColCount };

QSpinBox *depthBox(int value, QWidget *parent)
{
    auto *box = new QSpinBox(parent);
    box->setRange(0, kDepthSpinMaximum);
    box->setValue(value);
    return box;
}

QString moneyOr(const std::optional<qint64> &cents)
{
    return cents ? app::lookup::money(*cents) : QStringLiteral("-");
}

} // namespace

DevWindow::DevWindow(app::WorkFolder folder, QWidget *parent)
    : QWidget(parent), m_folder(std::move(folder)), m_url(new QLineEdit(QString::fromLatin1(kTestUrl), this)),
      m_browserLabel(new QLabel(this)), m_signIn(new QPushButton(QStringLiteral("Sign in to eBay"), this)),
      m_continue(new QPushButton(QStringLiteral("Continue"), this)),
      m_fetch(new QPushButton(QStringLiteral("Fetch test page"), this)),
      m_signOut(new QPushButton(QStringLiteral("Sign out"), this)), m_listing(new QLineEdit(this)),
      m_check(new QPushButton(QStringLiteral("Check listing"), this)), m_brand(new QLineEdit(this)),
      m_model(new QLineEdit(this)), m_productName(new QLineEdit(this)),
      m_saveIdentity(new QPushButton(QStringLiteral("Save identity"), this)), m_identityNote(new QLabel(this)),
      m_queries(new QPlainTextEdit(this)), m_expectation(new QLineEdit(this)), m_soldDepth(nullptr),
      m_activeDepth(nullptr), m_researchDepth(nullptr), m_fetchComps(new QPushButton(QStringLiteral("Fetch comps"), this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_status(new QLabel(this)),
      m_table(new QTreeWidget(this)), m_numbers(new QPlainTextEdit(this)), m_output(new QPlainTextEdit(this))
{
    setWindowTitle(QString::fromLatin1(app::version::kDisplayName) + QStringLiteral(" - developer window"));
    const app::Settings settings(m_folder.settingsPath());
    m_soldDepth = depthBox(settings.soldDepth(), this);
    m_activeDepth = depthBox(settings.activeDepth(), this);
    m_researchDepth = depthBox(settings.researchDepth(), this);
    m_listing->setPlaceholderText(QStringLiteral("https://www.ebay.com/itm/... or the 12-digit item number"));
    m_productName->setPlaceholderText(QStringLiteral("optional, e.g. a product line name"));
    m_expectation->setPlaceholderText(QStringLiteral("optional: what you think it is worth, shown next to the market numbers"));
    m_queries->setPlaceholderText(QStringLiteral("one search per line"));
    m_queries->setMaximumHeight(70);
    m_identityNote->setWordWrap(true);
    m_identityNote->setTextFormat(Qt::RichText);
    m_identityNote->setOpenExternalLinks(true);
    m_identityNote->setTextInteractionFlags(Qt::TextBrowserInteraction);
    const QFont fixed = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(fixed);
    m_numbers->setReadOnly(true);
    m_numbers->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_numbers->setFont(fixed);
    m_table->setColumnCount(ColCount);
    m_table->setHeaderLabels({QStringLiteral("use"), QStringLiteral("source"), QStringLiteral("total"),
                              QStringLiteral("eBay condition"), QStringLiteral("sold/format"), QStringLiteral("title"),
                              QStringLiteral("reasons")});
    m_table->setUniformRowHeights(true);
    m_table->header()->setStretchLastSection(true);
    m_status->setWordWrap(true);
    m_browserLabel->setWordWrap(true);
    m_browserLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    m_browserLabel->setOpenExternalLinks(true);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_signIn);
    buttons->addWidget(m_continue);
    buttons->addWidget(m_fetch);
    buttons->addWidget(m_signOut);
    buttons->addStretch(1);

    auto *listingRow = new QHBoxLayout;
    listingRow->addWidget(m_listing, 1);
    listingRow->addWidget(m_check);
    auto *identityRow = new QHBoxLayout;
    identityRow->addWidget(new QLabel(QStringLiteral("brand"), this));
    identityRow->addWidget(m_brand, 2);
    identityRow->addWidget(new QLabel(QStringLiteral("model"), this));
    identityRow->addWidget(m_model, 1);
    identityRow->addWidget(new QLabel(QStringLiteral("product name"), this));
    identityRow->addWidget(m_productName, 2);
    identityRow->addWidget(m_saveIdentity);
    auto *depthRow = new QHBoxLayout;
    depthRow->addWidget(new QLabel(QStringLiteral("sold"), this));
    depthRow->addWidget(m_soldDepth);
    depthRow->addWidget(new QLabel(QStringLiteral("for sale"), this));
    depthRow->addWidget(m_activeDepth);
    depthRow->addWidget(new QLabel(QStringLiteral("research rows"), this));
    depthRow->addWidget(m_researchDepth);
    depthRow->addStretch(1);
    depthRow->addWidget(m_fetchComps);
    depthRow->addWidget(m_cancel);
    auto *lookup = new QFormLayout;
    lookup->addRow(QStringLiteral("Listing:"), listingRow);
    lookup->addRow(QStringLiteral("Identity:"), identityRow);
    lookup->addRow(QString(), m_identityNote);
    lookup->addRow(QStringLiteral("Queries:"), m_queries);
    lookup->addRow(QStringLiteral("Your estimate:"), m_expectation);
    lookup->addRow(QStringLiteral("Depth:"), depthRow);

    auto *splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_table);
    splitter->addWidget(m_numbers);
    splitter->addWidget(m_output);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_browserLabel);
    layout->addWidget(new QLabel(QStringLiteral("Active search URL for \"Fetch test page\":"), this));
    layout->addWidget(m_url);
    layout->addLayout(buttons);
    layout->addLayout(lookup);
    layout->addWidget(m_status);
    layout->addWidget(splitter, 1);
    resize(1300, 950);

    connect(m_signIn, &QPushButton::clicked, this, [this]() { runSession(Task::SignIn); });
    connect(m_fetch, &QPushButton::clicked, this, [this]() { runSession(Task::Fetch); });
    connect(m_check, &QPushButton::clicked, this, &DevWindow::checkListing);
    connect(m_saveIdentity, &QPushButton::clicked, this, &DevWindow::saveIdentity);
    connect(m_fetchComps, &QPushButton::clicked, this, &DevWindow::fetchComps);
    connect(m_cancel, &QPushButton::clicked, this, &DevWindow::cancelLookup);
    connect(m_expectation, &QLineEdit::editingFinished, this, &DevWindow::recompute);
    connect(m_table, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_filling || column != ColUse || !m_rowOf.contains(item))
            return;
        m_rows[m_rowOf.value(item)].included = item->checkState(ColUse) == Qt::Checked;
        recompute();
    });
    connect(m_continue, &QPushButton::clicked, this, [this]() {
        if (m_controller) {
            m_continue->setEnabled(false);
            m_waitingForUser = false;
            m_status->setText(QStringLiteral("Browser session running..."));
            append(QStringLiteral("Continue pressed"));
            m_controller->continueAfterUser();
        }
    });
    connect(m_signOut, &QPushButton::clicked, this, &DevWindow::signOut);

    append(QStringLiteral("work folder: %1").arg(QDir::toNativeSeparators(m_folder.path())));
    append(pagesToday());
    QString error;
    m_known = app::lookup::KnownModels::builtIn(&error);
    if (m_known && !m_known->addUserFile(m_folder.knownModelsPath(), &error)) {
        append(QStringLiteral("your known-models file is not used: %1").arg(error));
        error.clear();
    }
    m_words = app::lookup::MatchWords::builtIn(&error);
    if (!m_known || !m_words)
        append(QStringLiteral("the matching data cannot be loaded, lookups are off: %1").arg(error));
    refreshBrowser();
    setBusy(false);
}

DevWindow::~DevWindow() = default;

QString DevWindow::pagesToday() const
{
    const app::Settings settings(m_folder.settingsPath());
    const app::PageBudget budget(m_folder.pageCountPath(), settings.dailyPageCap());
    QString error;
    const int used = budget.usedToday(&error);
    return used < 0 ? QStringLiteral("eBay pages loaded today: unknown (%1)").arg(error)
                    : QStringLiteral("eBay pages loaded today: %1 of %2").arg(used).arg(budget.dailyCap());
}

void DevWindow::refreshBrowser()
{
    const app::Settings settings(m_folder.settingsPath());
    m_browser = app::browser::BrowserCatalog::choose(app::browser::BrowserCatalog::installed(), settings.preferredBrowser());
    if (m_browser) {
        m_browserLabel->setText(QStringLiteral("Browser: %1 (%2)\nProfile folder: %3")
                                    .arg(m_browser->name, QDir::toNativeSeparators(m_browser->executable),
                                         QDir::toNativeSeparators(app::BrowserProfiles::profileDir(m_folder, m_browser->id))));
    } else {
        m_browserLabel->setText(QStringLiteral(
            "No supported browser is installed. Install one of them from its official page: "
            "<a href=\"https://brave.com/download/\">Brave</a>, <a href=\"https://www.google.com/chrome/\">Google "
            "Chrome</a>, <a href=\"https://www.microsoft.com/edge/download\">Microsoft Edge</a>."));
        m_browserLabel->setTextFormat(Qt::RichText);
    }
}

void DevWindow::runSession(Task task)
{
    refreshBrowser();
    if (!m_browser) {
        append(QStringLiteral("no supported browser installed"));
        return;
    }
    const app::Settings settings(m_folder.settingsPath());
    app::browser::BrowserControllerOptions options;
    options.pageDelayMinMs = settings.pageDelayMinMs();
    options.pageDelayMaxMs = settings.pageDelayMaxMs();
    options.dailyPageCap = settings.dailyPageCap();
    m_task = task;
    m_controller = new app::browser::BrowserController(m_folder, *m_browser, options, this);
    connect(m_controller, &app::browser::BrowserController::ready, this, &DevWindow::onReady);
    connect(m_controller, &app::browser::BrowserController::done, this, &DevWindow::onDone);
    connect(m_controller, &app::browser::BrowserController::needsUser, this, [this](const QString &message) {
        m_status->setText(message);
        m_waitingForUser = true;
        m_continue->setEnabled(true);
        append(QStringLiteral("waiting for you: %1").arg(message));
    });
    connect(m_controller, &app::browser::BrowserController::failed, this,
            [this](const QString &error) { append(QStringLiteral("browser session failed: %1").arg(error)); });
    connect(m_controller, &app::browser::BrowserController::finished, this, &DevWindow::onFinished);

    setBusy(true);
    const char *what = task == Task::SignIn ? "sign in" : task == Task::Fetch ? "fetch" : "lookup";
    append(QStringLiteral("starting %1 for: %2").arg(m_browser->name, QString::fromLatin1(what)));
    QString error;
    if (!m_controller->start(&error)) {
        append(QStringLiteral("cannot start: %1").arg(error));
        m_controller->deleteLater();
        m_controller = nullptr;
        setBusy(false);
    }
}

void DevWindow::onReady()
{
    append(QStringLiteral("%1 running, process id %2").arg(m_browser->name).arg(m_controller->processId()));
    if (m_task == Task::SignIn) {
        m_controller->signIn();
        return;
    }
    if (m_task == Task::Fetch) {
        m_controller->fetch(QUrl(m_url->text().trimmed()), app::ebay::PageKind::ActiveSearch);
        return;
    }
    m_runner = new app::lookup::LookupRunner(
        [this](const QUrl &url, app::ebay::PageKind kind) { m_controller->fetch(url, kind); }, &*m_known, &*m_words, this);
    connect(m_runner, &app::lookup::LookupRunner::progress, this, &DevWindow::append);
    connect(m_runner, &app::lookup::LookupRunner::itemReady, this, [this]() {
        m_result = m_runner->result();
        showIdentity();
        m_fetchComps->setEnabled(true);
        m_saveIdentity->setEnabled(true);
        m_status->setText(m_result.identity.confident
                              ? QStringLiteral("Check the identity and the queries (correct and Save if wrong), set the depths, "
                                               "then press Fetch comps.")
                              : QStringLiteral("The model is not known with confidence: enter the brand and model above and "
                                               "press Save identity. Nothing is searched before that."));
    });
    connect(m_runner, &app::lookup::LookupRunner::finished, this, [this]() {
        m_fetchComps->setEnabled(false);
        m_saveIdentity->setEnabled(false);
        m_result = m_runner->result();
        m_rows = app::lookup::classifyRows(m_result, *m_words);
        showRows();
        recompute();
        m_controller->stop();
    });
    QString error;
    if (!m_runner->startItem(m_listing->text(), &error)) {
        append(QStringLiteral("cannot check the listing: %1").arg(error));
        m_controller->stop();
    }
}

void DevWindow::onDone(const QJsonObject &reply)
{
    m_continue->setEnabled(false);
    m_waitingForUser = false;
    if (m_task == Task::Lookup) {
        if (m_runner)
            m_runner->onReply(reply);
        return;
    }
    append(QStringLiteral("reply:\n%1").arg(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Indented))));
    m_controller->stop();
}

void DevWindow::onFinished(bool normal)
{
    append(QStringLiteral("%1 closed (%2); the browser session is over")
               .arg(m_browser ? m_browser->name : QStringLiteral("browser"),
                    normal ? QStringLiteral("normal") : QStringLiteral("abnormal")));
    append(pagesToday());
    m_controller->deleteLater();
    m_controller = nullptr;
    if (m_runner) {
        m_runner->deleteLater();
        m_runner = nullptr;
    }
    m_waitingForUser = false;
    setBusy(false);
}

void DevWindow::checkListing()
{
    if (!m_known || !m_words) {
        append(QStringLiteral("the matching data cannot be loaded, lookups are off"));
        return;
    }
    if (app::ebay::itemIdFromInput(m_listing->text()).isEmpty()) {
        append(QStringLiteral("not an eBay item address (https://www.ebay.com/itm/<number>) or a 12-digit item number"));
        return;
    }
    // A new lookup starts clean: nothing of the previous one stays on screen.
    m_result = app::lookup::LookupResult();
    m_rows.clear();
    m_rowOf.clear();
    m_table->clear();
    m_numbers->clear();
    m_queries->clear();
    m_brand->clear();
    m_model->clear();
    m_productName->clear();
    m_identityNote->clear();
    runSession(Task::Lookup);
}

void DevWindow::showIdentity()
{
    const app::lookup::Identity &identity = m_result.identity;
    m_brand->setText(identity.brandName);
    m_model->setText(identity.model);
    m_productName->setText(identity.productName);
    m_queries->setPlainText(m_result.queries.join(QLatin1Char('\n')));
    QStringList lines;
    if (identity.confident) {
        lines << QStringLiteral("Identity from: %1.").arg(identity.source.toHtmlEscaped());
    } else {
        lines << QStringLiteral("<b>Model not known with confidence</b> (%1). Please enter it.").arg(identity.reason.toHtmlEscaped());
        lines << QStringLiteral("Title: %1").arg(m_result.item.title.toHtmlEscaped());
        QStringList specifics;
        for (const app::lookup::ItemRecord::Specific &specific : m_result.item.specifics)
            specifics << QStringLiteral("%1: %2").arg(specific.label, specific.value).toHtmlEscaped();
        if (!specifics.isEmpty())
            lines << QStringLiteral("Item specifics: %1").arg(specifics.join(QStringLiteral(" | ")));
        lines << QStringLiteral("<a href=\"%1\">Open the listing (photos)</a>")
                     .arg(app::ebay::itemUrl(m_result.item.itemId).toString(QUrl::FullyEncoded));
        if (!identity.candidates.isEmpty())
            lines << QStringLiteral("Suggestions: %1").arg(identity.candidates.join(QStringLiteral(", ")).toHtmlEscaped());
    }
    if (!identity.family.isEmpty())
        lines << QStringLiteral("Counts as the same item: %1").arg(identity.family.join(QStringLiteral(", ")).toHtmlEscaped());
    for (const QString &note : identity.notes)
        lines << QStringLiteral("Known issue: %1").arg(note.toHtmlEscaped());
    m_identityNote->setText(lines.join(QStringLiteral("<br>")));
}

void DevWindow::saveIdentity()
{
    if (!m_runner || !m_runner->isWaitingForQueries())
        return;
    const app::lookup::ConfirmedIdentity entered{m_brand->text(), m_model->text(), m_productName->text()};
    QString error;
    if (!m_known->saveConfirmed(m_folder.knownModelsPath(), m_result.item.itemId, entered, &error)) {
        append(QStringLiteral("identity not saved: %1").arg(error));
        return;
    }
    if (!m_runner->setIdentity(entered, &error)) {
        append(QStringLiteral("identity not used: %1").arg(error));
        return;
    }
    m_result = m_runner->result();
    showIdentity();
    append(QStringLiteral("identity saved for this listing: %1 %2").arg(m_result.identity.brandName, m_result.identity.model));
    m_status->setText(QStringLiteral("Identity saved. Check the queries, then press Fetch comps."));
}

void DevWindow::fetchComps()
{
    if (!m_runner || !m_runner->isWaitingForQueries())
        return;
    const app::lookup::Identity &identity = m_runner->result().identity;
    if (m_brand->text().simplified() != identity.brandName || m_model->text().simplified() != identity.model
        || m_productName->text().simplified() != identity.productName) {
        append(QStringLiteral("the identity fields were changed: press Save identity first (nothing was fetched)"));
        return;
    }
    const QStringList queries = m_queries->toPlainText().split(QLatin1Char('\n'));
    const app::lookup::Depths depths{m_soldDepth->value(), m_activeDepth->value(), m_researchDepth->value()};
    const app::Settings settings(m_folder.settingsPath());
    QString error;
    if (!m_runner->startComps(queries, depths, settings.soldHistoryDays(), &error)) {
        append(QStringLiteral("cannot fetch: %1").arg(error));
        return;
    }
    m_fetchComps->setEnabled(false);
    m_saveIdentity->setEnabled(false);
    m_status->setText(QStringLiteral("Fetching comparison pages..."));
}

void DevWindow::cancelLookup()
{
    if (!m_controller)
        return;
    append(QStringLiteral("Cancel pressed"));
    if (!m_runner) {
        m_controller->stop();
        return;
    }
    // While the browser waits for the user there is no page being loaded:
    // end that wait (the runner then gets "failed: stopped").
    if (m_waitingForUser && m_controller->isBusy()) {
        m_waitingForUser = false;
        m_controller->cancel();
        return;
    }
    m_runner->cancel();
}

void DevWindow::showRows()
{
    using app::lookup::Row;
    m_filling = true;
    m_table->clear();
    m_rowOf.clear();
    // By bucket, then by total price, lowest first (owner decision
    // 2026-10-19); the cheapest same-item listings for sale now are marked.
    QTreeWidgetItem *group = nullptr;
    std::optional<app::lookup::Bucket> current;
    int cheapest = 0;
    auto closeGroup = [&]() {
        if (group && current) {
            group->setText(ColUse, QStringLiteral("%1 (%2)").arg(app::lookup::bucketName(*current)).arg(group->childCount()));
            group->setExpanded(*current != app::lookup::Bucket::NoMatch);
        }
    };
    for (const qsizetype i : app::lookup::rowOrder(m_result, m_rows)) {
        const Row &row = m_rows.at(i);
        if (!current || row.match.bucket != *current) {
            closeGroup();
            current = row.match.bucket;
            group = new QTreeWidgetItem(m_table);
            group->setFirstColumnSpanned(true);
        }
        auto *item = new QTreeWidgetItem(group);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(ColUse, row.included ? Qt::Checked : Qt::Unchecked);
        item->setText(ColTotal, moneyOr(app::lookup::rowTotal(m_result, row)));
        item->setText(ColCondition, app::lookup::rowCondition(m_result, row));
        item->setText(ColReasons, row.match.reasons.join(QStringLiteral("; ")));
        QString title = app::lookup::rowTitle(m_result, row);
        if (row.source == Row::Source::Research) {
            item->setText(ColSource, QStringLiteral("research"));
            item->setText(ColExtra, m_result.research.at(row.index).lastSoldDate);
        } else {
            const bool sold = row.source == Row::Source::Sold;
            const app::lookup::SearchRecord &record = sold ? m_result.sold.at(row.index) : m_result.active.at(row.index);
            item->setText(ColSource, sold ? (record.confirmed ? QStringLiteral("sold") : QStringLiteral("sold, unconfirmed"))
                                          : QStringLiteral("for sale"));
            item->setText(ColExtra, sold ? record.soldDate : record.format);
            if (!sold && record.itemId == m_result.item.itemId)
                title = QStringLiteral("[THIS LISTING] ") + title;
            else if (!sold && row.included && app::lookup::rowTotal(m_result, row) && cheapest < app::lookup::kCheapestMarked)
                title = QStringLiteral("[CHEAPEST %1] ").arg(++cheapest) + title;
        }
        item->setText(ColTitle, title);
        m_rowOf.insert(item, i);
    }
    closeGroup();
    for (int column = 0; column < ColReasons; ++column)
        m_table->resizeColumnToContents(column);
    m_filling = false;
}

void DevWindow::recompute()
{
    if (!m_words || m_result.status == QLatin1String("running"))
        return;
    const app::Settings settings(m_folder.settingsPath());
    const app::lookup::Valuation valuation = app::lookup::valuate(m_result, m_rows, settings.valueSettings());
    m_numbers->setPlainText(app::lookup::reportText(m_result, m_rows, valuation, m_expectation->text()));
}

void DevWindow::signOut()
{
    refreshBrowser();
    if (!m_browser) {
        append(QStringLiteral("no supported browser installed"));
        return;
    }
    QString error;
    if (app::BrowserProfiles::clear(m_folder, m_browser->id, &error))
        append(QStringLiteral("signed out: the %1 profile folder in the work folder was deleted (no browser was "
                              "started)").arg(m_browser->name));
    else
        append(QStringLiteral("sign out failed: %1").arg(error));
}

void DevWindow::setBusy(bool busy)
{
    m_signIn->setEnabled(!busy);
    m_fetch->setEnabled(!busy);
    m_signOut->setEnabled(!busy);
    m_check->setEnabled(!busy);
    m_cancel->setEnabled(busy);
    if (!busy) {
        m_continue->setEnabled(false);
        m_fetchComps->setEnabled(false);
        m_saveIdentity->setEnabled(false);
    }
    m_status->setText(busy ? QStringLiteral("Browser session running...") : QStringLiteral("No browser session."));
}

void DevWindow::append(const QString &line)
{
    m_output->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
}
