#include "DevWindow.h"

#include "browser/BrowserController.h"
#include "core/BrowserProfiles.h"
#include "core/Settings.h"
#include "core/Version.h"

#include <QDateTime>
#include <QDir>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

namespace {

// One active search for a common item, 240 results per page.
const char kTestUrl[] = "https://www.ebay.com/sch/i.html?_nkw=usb+c+cable&_ipg=240";

} // namespace

DevWindow::DevWindow(app::WorkFolder folder, QWidget *parent)
    : QWidget(parent), m_folder(std::move(folder)), m_url(new QLineEdit(QString::fromLatin1(kTestUrl), this)),
      m_browserLabel(new QLabel(this)), m_signIn(new QPushButton(QStringLiteral("Sign in to eBay"), this)),
      m_continue(new QPushButton(QStringLiteral("Continue"), this)),
      m_fetch(new QPushButton(QStringLiteral("Fetch test page"), this)),
      m_signOut(new QPushButton(QStringLiteral("Sign out"), this)), m_status(new QLabel(this)),
      m_output(new QPlainTextEdit(this))
{
    setWindowTitle(QString::fromLatin1(app::version::kDisplayName) + QStringLiteral(" - developer window"));
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);
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
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_browserLabel);
    layout->addWidget(new QLabel(QStringLiteral("Active search URL for \"Fetch test page\":"), this));
    layout->addWidget(m_url);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addWidget(m_output, 1);
    resize(900, 700);

    connect(m_signIn, &QPushButton::clicked, this, [this]() { runSession(Task::SignIn); });
    connect(m_fetch, &QPushButton::clicked, this, [this]() { runSession(Task::Fetch); });
    connect(m_continue, &QPushButton::clicked, this, [this]() {
        if (m_controller) {
            m_continue->setEnabled(false);
            m_status->setText(QStringLiteral("Browser session running..."));
            append(QStringLiteral("Continue pressed"));
            m_controller->continueAfterUser();
        }
    });
    connect(m_signOut, &QPushButton::clicked, this, &DevWindow::signOut);

    append(QStringLiteral("work folder: %1").arg(QDir::toNativeSeparators(m_folder.path())));
    refreshBrowser();
    setBusy(false);
}

DevWindow::~DevWindow() = default;

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
    m_task = task;
    m_controller = new app::browser::BrowserController(m_folder, *m_browser, options, this);
    connect(m_controller, &app::browser::BrowserController::ready, this, &DevWindow::onReady);
    connect(m_controller, &app::browser::BrowserController::done, this, &DevWindow::onDone);
    connect(m_controller, &app::browser::BrowserController::needsUser, this, [this](const QString &message) {
        m_status->setText(message);
        m_continue->setEnabled(true);
        append(QStringLiteral("waiting for you: %1").arg(message));
    });
    connect(m_controller, &app::browser::BrowserController::failed, this,
            [this](const QString &error) { append(QStringLiteral("browser session failed: %1").arg(error)); });
    connect(m_controller, &app::browser::BrowserController::finished, this, &DevWindow::onFinished);

    setBusy(true);
    append(QStringLiteral("starting %1 for: %2").arg(m_browser->name, task == Task::SignIn ? QStringLiteral("sign in")
                                                                                             : QStringLiteral("fetch")));
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
    if (m_task == Task::SignIn)
        m_controller->signIn();
    else
        m_controller->fetch(QUrl(m_url->text().trimmed()), app::ebay::PageKind::ActiveSearch);
}

void DevWindow::onDone(const QJsonObject &reply)
{
    m_continue->setEnabled(false);
    append(QStringLiteral("reply:\n%1").arg(QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Indented))));
    m_controller->stop();
}

void DevWindow::onFinished(bool normal)
{
    append(QStringLiteral("%1 closed (%2); the browser session is over")
               .arg(m_browser ? m_browser->name : QStringLiteral("browser"),
                    normal ? QStringLiteral("normal") : QStringLiteral("abnormal")));
    m_controller->deleteLater();
    m_controller = nullptr;
    setBusy(false);
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
    if (!busy)
        m_continue->setEnabled(false);
    m_status->setText(busy ? QStringLiteral("Browser session running...") : QStringLiteral("No browser session."));
}

void DevWindow::append(const QString &line)
{
    m_output->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
}
