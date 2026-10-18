#include "DevWindow.h"

#include "core/BrowserProtocol.h"
#include "core/Version.h"
#include "core/WebEngineData.h"

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

DevWindow::DevWindow(app::WorkFolder folder, QString workerProgram, QWidget *parent)
    : QWidget(parent), m_folder(folder), m_worker(std::move(folder), std::move(workerProgram)),
      m_url(new QLineEdit(QString::fromLatin1(kTestUrl), this)),
      m_signIn(new QPushButton(QStringLiteral("Sign in to eBay"), this)),
      m_fetch(new QPushButton(QStringLiteral("Fetch test page"), this)),
      m_signOut(new QPushButton(QStringLiteral("Sign out"), this)), m_status(new QLabel(this)),
      m_output(new QPlainTextEdit(this))
{
    setWindowTitle(QString::fromLatin1(app::version::kDisplayName) + QStringLiteral(" - developer window"));
    m_output->setReadOnly(true);
    m_output->setLineWrapMode(QPlainTextEdit::NoWrap);

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(m_signIn);
    buttons->addWidget(m_fetch);
    buttons->addWidget(m_signOut);
    buttons->addStretch(1);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Active search URL for \"Fetch test page\":"), this));
    layout->addWidget(m_url);
    layout->addLayout(buttons);
    layout->addWidget(m_status);
    layout->addWidget(m_output, 1);
    resize(900, 700);

    connect(m_signIn, &QPushButton::clicked, this, [this]() {
        runSession(QJsonObject{{QStringLiteral("cmd"), QString::fromLatin1(app::browserprotocol::kCmdSignIn)}});
    });
    connect(m_fetch, &QPushButton::clicked, this, [this]() {
        runSession(QJsonObject{{QStringLiteral("cmd"), QString::fromLatin1(app::browserprotocol::kCmdFetch)},
                               {QStringLiteral("kind"), QStringLiteral("activeSearch")},
                               {QStringLiteral("url"), m_url->text().trimmed()}});
    });
    connect(m_signOut, &QPushButton::clicked, this, &DevWindow::signOut);
    connect(&m_worker, &app::BrowserWorker::ready, this, &DevWindow::onReady);
    connect(&m_worker, &app::BrowserWorker::replied, this, &DevWindow::onReplied);
    connect(&m_worker, &app::BrowserWorker::failed, this, &DevWindow::onFailed);
    connect(&m_worker, &app::BrowserWorker::finished, this, &DevWindow::onFinished);

    append(QStringLiteral("work folder: %1").arg(QDir::toNativeSeparators(m_folder.path())));
    setBusy(false);
}

void DevWindow::runSession(const QJsonObject &command)
{
    m_command = command;
    setBusy(true);
    append(QStringLiteral("starting the browser worker for: %1").arg(command.value(QStringLiteral("cmd")).toString()));
    QString error;
    if (!m_worker.start(&error)) {
        append(QStringLiteral("cannot start: %1").arg(error));
        setBusy(false);
    }
}

void DevWindow::onReady()
{
    append(QStringLiteral("browser worker running, process id %1").arg(m_worker.processId()));
    if (m_worker.request(m_command) < 0) {
        append(QStringLiteral("request refused"));
        m_worker.stop();
    }
}

void DevWindow::onReplied(int id, const QJsonObject &reply)
{
    append(QStringLiteral("reply %1:\n%2").arg(id).arg(
        QString::fromUtf8(QJsonDocument(reply).toJson(QJsonDocument::Indented))));
    if (!m_worker.isBusy())
        m_worker.stop();
}

void DevWindow::onFailed(const QString &error)
{
    append(QStringLiteral("browser worker failed: %1").arg(error));
}

void DevWindow::onFinished(bool normal, int exitCode)
{
    append(QStringLiteral("browser worker exited (%1, exit code %2); the browser session is over")
               .arg(normal ? QStringLiteral("normal") : QStringLiteral("abnormal"))
               .arg(exitCode));
    setBusy(false);
}

void DevWindow::signOut()
{
    QString error;
    if (app::WebEngineData::clear(m_folder, &error))
        append(QStringLiteral("signed out: browser cookies, storage and cache deleted (no browser was started)"));
    else
        append(QStringLiteral("sign out failed: %1").arg(error));
}

void DevWindow::setBusy(bool busy)
{
    m_signIn->setEnabled(!busy);
    m_fetch->setEnabled(!busy);
    m_signOut->setEnabled(!busy);
    m_status->setText(busy ? QStringLiteral("Browser session running...") : QStringLiteral("No browser session."));
}

void DevWindow::append(const QString &line)
{
    m_output->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss ")) + line);
}
