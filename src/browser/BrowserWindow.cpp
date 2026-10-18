#include "browser/BrowserWindow.h"

#include "core/Version.h"

#include <QCloseEvent>
#include <QLabel>
#include <QVBoxLayout>
#include <QWebEngineView>

namespace app::browser {

BrowserWindow::BrowserWindow(QWebEnginePage *page, QWidget *parent)
    : QWidget(parent), m_message(new QLabel(this)), m_view(new QWebEngineView(this))
{
    setWindowTitle(QString::fromLatin1(version::kDisplayName) + QStringLiteral(" - eBay"));
    m_message->setWordWrap(true);
    m_message->setTextFormat(Qt::PlainText);
    m_message->setMargin(8);
    m_view->setPage(page);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_message);
    layout->addWidget(m_view, 1);
    resize(1100, 800);
}

BrowserWindow::~BrowserWindow() = default;

void BrowserWindow::showWithMessage(const QString &message)
{
    setMessage(message);
    show();
    raise();
    activateWindow();
}

void BrowserWindow::setMessage(const QString &message)
{
    m_message->setText(message);
}

void BrowserWindow::releasePage()
{
    m_view->setPage(nullptr);
}

void BrowserWindow::closeEvent(QCloseEvent *event)
{
    // The window only hides; the session decides when it is deleted.
    event->ignore();
    hide();
    emit closed();
}

} // namespace app::browser
