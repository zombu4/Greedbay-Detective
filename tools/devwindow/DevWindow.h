#pragma once

#include "core/BrowserWorker.h"
#include "core/WorkFolder.h"

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// Three buttons around one browser session each:
//   Sign in to eBay   starts the worker, asks it to sign in, ends the session
//   Fetch test page   starts the worker, fetches the URL in the field as an
//                     active search, shows the extracted JSON, ends the session
//   Sign out          deletes the browser data without starting the browser
// Every step is written to the text area, including the worker's process id
// and how it exited.
class DevWindow : public QWidget {
    Q_OBJECT
public:
    DevWindow(app::WorkFolder folder, QString workerProgram, QWidget *parent = nullptr);

private:
    void runSession(const QJsonObject &command);
    void onReady();
    void onReplied(int id, const QJsonObject &reply);
    void onFailed(const QString &error);
    void onFinished(bool normal, int exitCode);
    void signOut();
    void setBusy(bool busy);
    void append(const QString &line);

    app::WorkFolder m_folder;
    app::BrowserWorker m_worker;
    QJsonObject m_command;
    QLineEdit *m_url;
    QPushButton *m_signIn;
    QPushButton *m_fetch;
    QPushButton *m_signOut;
    QLabel *m_status;
    QPlainTextEdit *m_output;
};
