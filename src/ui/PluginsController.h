#pragma once

#include "core/WorkFolder.h"
#include "plugins/PluginCatalog.h"
#include "plugins/PluginHost.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

namespace app::ui {

// The Plugins part of Settings (DESIGN 4.3 screen 6, PLAN 15): every plugin
// with its type, version, author, description and permissions in plain
// words, whether it is enabled, and why the app disabled it if it did;
// enable / disable (the screen shows the permissions before enabling),
// install from a folder or a .zip (dropped on the screen or typed), remove
// an installed one, and the time limit per call. Every change reloads the
// running plugins. Each action returns an empty string when it worked, else
// why not in plain words.
class PluginsController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by the app")
    // [{id, name, version, author, description, type, typeText, permissions
    // (plain words), builtIn, enabled, failure}]
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY changed)
    // Installed folders that cannot be used, and why.
    Q_PROPERTY(QStringList problems READ problems NOTIFY changed)
    Q_PROPERTY(int timeLimitMs READ timeLimitMs NOTIFY changed)
    Q_PROPERTY(int minTimeLimitMs READ minTimeLimitMs CONSTANT)
    Q_PROPERTY(int maxTimeLimitMs READ maxTimeLimitMs CONSTANT)
    Q_PROPERTY(QString pluginsFolder READ pluginsFolder CONSTANT)
public:
    // `catalog` and `host` must outlive the controller.
    PluginsController(WorkFolder folder, const plugins::PluginCatalog &catalog, plugins::PluginHost &host,
                      QObject *parent = nullptr);

    QVariantList plugins() const { return m_plugins; }
    QStringList problems() const { return m_problems; }
    int timeLimitMs() const;
    int minTimeLimitMs() const;
    int maxTimeLimitMs() const;
    QString pluginsFolder() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString setEnabled(const QString &id, bool enabled);
    // `pathOrUrl`: a path, or a file:// URL as a drop gives it.
    Q_INVOKABLE QString install(const QString &pathOrUrl);
    Q_INVOKABLE QString remove(const QString &id);
    Q_INVOKABLE QString setTimeLimitMs(int ms);

signals:
    void changed();

private:
    // Reloads the running plugins, then the list.
    void reloadAll();

    WorkFolder m_folder;
    const plugins::PluginCatalog &m_catalog;
    plugins::PluginHost &m_host;
    QVariantList m_plugins;
    QStringList m_problems;
};

} // namespace app::ui
