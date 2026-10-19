#include "DevWindow.h"

#include "browser/BrowserController.h"
#include "core/BrowserProfiles.h"
#include "core/PageBudget.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "ebay/SearchUrls.h"
#include "lookup/LookupReport.h"
#include "lookup/LookupRunner.h"

#include <QDateTime>
#include <QDir>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <utility>

namespace {

// One active search for a common item, 240 results per page.
const char kTestUrl[] = "https://www.ebay.com/sch/i.html?_nkw=usb+c+cable&_ipg=240";

// The spin boxes need a maximum; the setting itself has none (the daily page
// cap limits the loads).
constexpr int kDepthSpinMaximum = 100000;

QSpinBox *depthBox(int value, QWidget *parent)
{
    auto *box = new QSpinBox(parent);
    box->setRange(0, kDepthSpinMaximum);
    box->setValue(value);
    return box;
}

} // namespace

DevWindow::DevWindow(app::WorkFolder folder, QWidget *parent)
    : QWidget(parent), m_folder(std::move(folder)), m_url(new QLineEdit(QString::fromLatin1(kTestUrl), this)),
      m_browserLabel(new QLabel(this)), m_signIn(new QPushButton(QStringLiteral("Sign in to eBay"), this)),
      m_continue(new QPushButton(QStringLiteral("Continue"), this)),
      m_fetch(new QPushButton(QStringLiteral("Fetch test page"), this)),
      m_signOut(new QPushButton(QStringLiteral("Sign out"), this)), m_listing(new QLineEdit(this)),
      m_check(new QPushButton(QStringLiteral("Check listing"), this)), m_soldDepth(nullptr), m_activeDepth(nullptr),
      m_researchDepth(nullptr), m_fetchComps(new QPushButton(QStringLiteral("Fetch comps"), this)),
      m_cancel(new QPushButton(QStringLiteral("Cancel"), this)), m_status(new QLabel(this)),
      m_output(new QPlainTextEdit(this))
{
    setWindowTitle(QString::fromLatin1(app::version::kDisplayName) + QStringLiteral(" - developer window"));
    const app::Settings settings(m_folder.settingsPath());
    m_soldDepth = depthBox(settings.soldDepth(), this);
    m_activeDepth = depthBox(settings.activeDepth(), this);
    m_researchDepth = depthBox(settings.researchDepth(), this);
    for (QLineEdit *&query : m_queries)
        query = new QLineEdit(this);
    m_listing->setPlaceholderText(QStringLiteral("https://www.ebay.com/itm/... or the 12-digit item number"));
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_output->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
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
    for (std::size_t i = 0; i < m_queries.size(); ++i)
        lookup->addRow(QStringLiteral("Query %1:").arg(i + 1), m_queries.at(i));
    lookup->addRow(QStringLiteral("Depth:"), depthRow);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_browserLabel);
    layout->addWidget(new QLabel(QStringLiteral("Active search URL for \"Fetch test page\":"), this));
    layout->addWidget(m_url);
    layout->addLayout(buttons);
    layout->addLayout(lookup);
    layout->addWidget(m_status);
    layout->addWidget(m_output, 1);
    resize(1100, 800);

    connect(m_signIn, &QPushButton::clicked, this, [this]() { runSession(Task::SignIn); });
    connect(m_fetch, &QPushButton::clicked, this, [this]() { runSession(Task::Fetch); });
    connect(m_check, &QPushButton::clicked, this, &DevWindow::checkListing);
    connect(m_fetchComps, &QPushButton::clicked, this, &DevWindow::fetchComps);
    connect(m_cancel, &QPushButton::clicked, this, &DevWindow::cancelLookup);
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
        [this](const QUrl &url, app::ebay::PageKind kind) { m_controller->fetch(url, kind); }, this);
    connect(m_runner, &app::lookup::LookupRunner::progress, this, &DevWindow::append);
    connect(m_runner, &app::lookup::LookupRunner::itemReady, this, [this]() {
        const QStringList queries = m_runner->result().queries;
        for (std::size_t i = 0; i < m_queries.size(); ++i) {
            m_queries.at(i)->setText(static_cast<qsizetype>(i) < queries.size() ? queries.at(static_cast<qsizetype>(i)) : QString());
            m_queries.at(i)->setEnabled(true);
        }
        m_fetchComps->setEnabled(true);
        m_status->setText(QStringLiteral("Check the search queries (edit or clear them), set the depths, then press "
                                         "Fetch comps."));
    });
    connect(m_runner, &app::lookup::LookupRunner::finished, this, [this]() {
        m_fetchComps->setEnabled(false);
        append(QStringLiteral("report:\n%1").arg(app::lookup::reportText(m_runner->result())));
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
    if (app::ebay::itemIdFromInput(m_listing->text()).isEmpty()) {
        append(QStringLiteral("not an eBay item address (https://www.ebay.com/itm/<number>) or a 12-digit item number"));
        return;
    }
    for (QLineEdit *query : m_queries)
        query->clear();
    runSession(Task::Lookup);
}

void DevWindow::fetchComps()
{
    if (!m_runner || !m_runner->isWaitingForQueries())
        return;
    QStringList queries;
    for (QLineEdit *query : m_queries)
        queries.append(query->text());
    const app::lookup::Depths depths{m_soldDepth->value(), m_activeDepth->value(), m_researchDepth->value()};
    const app::Settings settings(m_folder.settingsPath());
    QString error;
    if (!m_runner->startComps(queries, depths, settings.soldHistoryDays(), &error)) {
        append(QStringLiteral("cannot fetch: %1").arg(error));
        return;
    }
    m_fetchComps->setEnabled(false);
    for (QLineEdit *query : m_queries)
        query->setEnabled(false);
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
    }
    m_status->setText(busy ? QStringLiteral("Browser session running...") : QStringLiteral("No browser session."));
}

void DevWindow::append(const QString &line)
{
    m_output->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
}
