#pragma once

#include <QWidget>

class QLabel;
class QWebEnginePage;
class QWebEngineView;

namespace app::browser {

// The visible browser window: a plain message line above the page. It is
// shown only when the user has to act (sign in, or look at a page that is
// not the expected one); otherwise the page loads with the window hidden.
// Closing the window is the user's "done" and is reported by closed().
class BrowserWindow : public QWidget {
    Q_OBJECT
public:
    explicit BrowserWindow(QWebEnginePage *page, QWidget *parent = nullptr);
    ~BrowserWindow() override;

    // Shows the window with `message` above the page and brings it to the front.
    void showWithMessage(const QString &message);
    void setMessage(const QString &message);

    // Detaches the page from the view, so the page can be deleted after the
    // window without the view referring to it.
    void releasePage();

signals:
    void closed();

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QLabel *m_message;
    QWebEngineView *m_view;
};

} // namespace app::browser
