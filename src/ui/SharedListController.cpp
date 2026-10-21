#include "ui/SharedListController.h"

#include "core/Settings.h"
#include "lookup/Sellers.h"
#include "sellerlist/Crypto.h"

#include <QFile>
#include <QJsonDocument>
#include <QLocale>
#include <QNetworkProxy>

namespace app::ui {

namespace sl = sellerlist;

namespace {

constexpr qint64 kDaySeconds = 24 * 60 * 60;

// The list owner's public keys and onion address, compiled into the app
// from data/master.json; std::nullopt when this build has none.
std::optional<sl::MasterInfo> masterInfo()
{
    QFile file(QStringLiteral(":/sellerlist/master.json"));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return sl::MasterInfo::fromJson(QJsonDocument::fromJson(file.readAll()).object());
}

QString serverAnswer(const QString &result, const QString &why)
{
    if (result == QLatin1String("received"))
        return QObject::tr("Sent. The shared list shows it once the server has checked the item (usually within a few "
                           "minutes; the app updates the list at most once a day, or with Update now).");
    if (result == QLatin1String("held"))
        return QObject::tr("Sent and held for the list owner to look at: %1").arg(why);
    if (result == QLatin1String("busy"))
        return QObject::tr("The server takes no more right now: %1. Try again tomorrow.").arg(why);
    return QObject::tr("Not sent: %1").arg(why);
}

} // namespace

SharedListController::SharedListController(WorkFolder folder, std::function<bool()> lookupRunning, QObject *parent)
    : QObject(parent),
      m_folder(std::move(folder)),
      m_lookupRunning(std::move(lookupRunning)),
      m_tor(sl::TorRunner::bundledProgram(), m_folder.torDir()),
      m_purchase(m_folder)
{
    // Nothing goes out without tor: the proxy is set only while tor is ready.
    m_network.setProxy(QNetworkProxy::NoProxy);
    if (const std::optional<sl::MasterInfo> master = masterInfo()) {
        m_client = new sl::ListClient(&m_network, *master, m_folder.sharedListDir(), this);
        connect(m_client, &sl::ListClient::listFetched, this, [this](bool ok, const QString &why) {
            m_lastError = ok ? QString() : why;
            if (ok) {
                ++m_revision;
                emit listChanged();
            }
            jobDone();
        });
        connect(m_client, &sl::ListClient::reportSent, this, [this](bool ok, const QString &result, const QString &why) {
            setReport(false, serverAnswer(result, why), ok);
            jobDone();
        });
        connect(m_client, &sl::ListClient::disputeSent, this, [this](bool ok, const QString &result, const QString &why) {
            m_disputeOk = ok;
            m_disputeStatus = ok ? tr("Sent to the list owner.") : serverAnswer(result, why);
            jobDone();
        });
    }
    connect(&m_tor, &sl::TorRunner::stateChanged, this, &SharedListController::onTorState);
    connect(&m_purchase, &PurchaseCheck::changed, this, &SharedListController::changed);
    connect(&m_purchase, &PurchaseCheck::finished, this, &SharedListController::onPurchaseFinished);
}

SharedListController::~SharedListController() { m_tor.stop(); }

bool SharedListController::readEnabled() const { return Settings(m_folder.settingsPath()).sharedListRead(); }

bool SharedListController::reportEnabled() const { return Settings(m_folder.settingsPath()).sharedListReport(); }

void SharedListController::startUp()
{
    if (!m_client)
        return;
    QString why;
    if (m_client->loadCache(QDateTime::currentDateTimeUtc(), &why)) {
        ++m_revision;
        emit listChanged();
    } else if (QFile::exists(m_folder.sharedListDir() + QStringLiteral("/list.json"))) {
        m_lastError = why;
    }
    if (readEnabled()
        && (!m_client->list() || m_client->fetchedAt().secsTo(QDateTime::currentDateTimeUtc()) >= kDaySeconds))
        refresh();
    emit changed();
}

void SharedListController::setReadEnabled(bool enabled)
{
    Settings settings(m_folder.settingsPath());
    settings.setSharedListRead(enabled);
    settings.sync();
    ++m_revision;
    emit listChanged();
    emit changed();
}

void SharedListController::setReportEnabled(bool enabled)
{
    Settings settings(m_folder.settingsPath());
    settings.setSharedListReport(enabled);
    settings.sync();
    emit changed();
}

QString SharedListController::statusText() const
{
    if (!m_client)
        return tr("The shared seller list is not set up in this build of the app yet.");
    if (!readEnabled())
        return tr("Off.");
    QString text;
    if (m_job != Job::None && m_tor.state() == sl::TorRunner::State::Starting)
        text = tr("Updating: %1...").arg(m_tor.progress());
    else if (m_job == Job::Fetch)
        text = tr("Updating...");
    else if (m_client->list())
        text = tr("Updated %1 - %n seller(s) on the list.", nullptr, static_cast<int>(m_client->list()->list.sellers.size()))
                   .arg(QLocale().toString(m_client->fetchedAt().toLocalTime(), QStringLiteral("yyyy-MM-dd HH:mm")));
    else
        text = tr("Not updated yet.");
    if (!m_lastError.isEmpty() && m_job == Job::None)
        text += QLatin1Char(' ') + tr("Shared list not updated (%1).").arg(m_lastError);
    return text;
}

QVariantList SharedListController::reportsFor(const QString &sellerId) const
{
    QVariantList out;
    if (!m_client || !readEnabled() || !m_client->list())
        return out;
    const sl::ListedSeller *seller = m_client->list()->list.find(sellerId);
    if (!seller)
        return out;
    for (const sl::PublishedReport &report : seller->reports) {
        out.append(QVariantMap{{QStringLiteral("id"), report.id},
                               {QStringLiteral("reason"), report.reason},
                               {QStringLiteral("reasonLabel"), lookup::markReasonLabel(report.reason)},
                               {QStringLiteral("text"), report.text},
                               {QStringLiteral("month"), report.month},
                               {QStringLiteral("verified"), report.verifiedPurchase}});
    }
    return out;
}

QHash<QString, QStringList> SharedListController::linesBySeller() const
{
    QHash<QString, QStringList> lines;
    if (!m_client || !readEnabled() || !m_client->list())
        return lines;
    for (const sl::ListedSeller &seller : m_client->list()->list.sellers) {
        QStringList &sellerLines = lines[seller.sellerId.toLower()];
        for (const sl::PublishedReport &report : seller.reports)
            sellerLines.append(tr("%1 (bought %2): %3").arg(lookup::markReasonLabel(report.reason), report.month, report.text));
    }
    return lines;
}

void SharedListController::refresh()
{
    if (!m_client || !readEnabled() || m_job != Job::None)
        return;
    runWithTor(Job::Fetch);
}

void SharedListController::report(const QString &sellerId, const QString &reason, const QString &text)
{
    if (!m_client || !reportEnabled()) {
        setReport(false, tr("Reporting is off (Settings, Shared seller list)."), false);
        return;
    }
    if (m_reportBusy || m_job != Job::None) {
        setReport(m_reportBusy, tr("Another shared-list request is still running."), false);
        return;
    }
    if (m_lookupRunning && m_lookupRunning()) {
        setReport(false, tr("Wait until the lookup is finished: the purchase check uses the same browser."), false);
        return;
    }
    // The user's part is checked before any page is loaded; the item, the
    // month, the time and the installation key are filled in later.
    if (!lookup::isSellerId(sellerId) || !lookup::isMarkReason(reason) || !sl::isCleanText(text, sl::kMaxReportText)) {
        setReport(false, tr("Choose a reason and write what happened (at most %1 characters).").arg(sl::kMaxReportText), false);
        return;
    }
    m_report = sl::Report{sellerId, reason, text.trimmed(), {}, {}, {}, {}, {}};
    m_reportSellerId = sellerId;
    if (!m_purchase.check(sellerId)) {
        m_report.reset();
        setReport(false, m_purchase.error(), false);
        return;
    }
    setReport(true, tr("Looking for an order from this seller in your eBay purchases..."), false);
}

void SharedListController::onPurchaseFinished()
{
    if (!m_report)
        return;
    const std::optional<PurchaseCheck::Found> found = m_purchase.found();
    if (!found) {
        m_report.reset();
        setReport(false, m_purchase.error().isEmpty() ? tr("Stopped.") : m_purchase.error(), false);
        return;
    }
    m_report->itemId = found->itemId;
    m_report->month = found->month;
    setReport(true, tr("Purchase found (item %1, %2). Sending over Tor...").arg(found->itemId, found->month), false);
    runWithTor(Job::Report);
}

void SharedListController::cancelReport()
{
    if (m_purchase.busy())
        m_purchase.cancel();
    if (m_job == Job::Report && m_tor.state() == sl::TorRunner::State::Starting) {
        m_report.reset();
        setReport(false, tr("Stopped."), false);
        jobDone();
    }
}

void SharedListController::dispute(const QString &sellerId, const QString &reportId, const QString &text)
{
    m_disputeOk = false;
    if (!m_client) {
        m_disputeStatus = tr("The shared seller list is not set up in this build of the app yet.");
        emit changed();
        return;
    }
    if (m_job != Job::None) {
        m_disputeStatus = tr("Another shared-list request is still running.");
        emit changed();
        return;
    }
    const sl::Dispute dispute{sellerId, reportId, text.trimmed()};
    const QString problem = dispute.problem();
    if (!problem.isEmpty()) {
        m_disputeStatus = tr("Write what is wrong (at most %1 characters).").arg(sl::kMaxDisputeText);
        emit changed();
        return;
    }
    m_dispute = dispute;
    m_disputeStatus = tr("Sending over Tor...");
    runWithTor(Job::Dispute);
}

void SharedListController::runWithTor(Job job)
{
    m_job = job;
    emit changed();
    if (m_tor.state() == sl::TorRunner::State::Ready) {
        go();
        return;
    }
    m_tor.start(); // onTorState continues
    onTorState();
}

void SharedListController::onTorState()
{
    if (m_job == Job::None) {
        emit changed();
        return;
    }
    switch (m_tor.state()) {
    case sl::TorRunner::State::Ready:
        go();
        return;
    case sl::TorRunner::State::Failed: {
        const QString why = tr("Tor unavailable: %1").arg(m_tor.error());
        const Job job = m_job;
        if (job == Job::Fetch)
            m_lastError = why;
        else if (job == Job::Report)
            setReport(false, tr("Not sent (%1).").arg(why), false);
        else
            m_disputeStatus = tr("Not sent (%1).").arg(why);
        m_report.reset();
        m_dispute.reset();
        jobDone();
        return;
    }
    case sl::TorRunner::State::Starting:
    case sl::TorRunner::State::Off:
        emit changed();
        return;
    }
}

void SharedListController::go()
{
    m_network.setProxy(m_tor.proxy());
    switch (m_job) {
    case Job::Fetch:
        m_client->fetchList();
        return;
    case Job::Report: {
        const sl::Report report = *m_report;
        m_report.reset();
        m_client->sendReport(report);
        return;
    }
    case Job::Dispute: {
        const sl::Dispute dispute = *m_dispute;
        m_dispute.reset();
        m_client->sendDispute(dispute);
        return;
    }
    case Job::None:
        return;
    }
}

void SharedListController::jobDone()
{
    m_job = Job::None;
    // tor runs only while a request needs it.
    m_network.setProxy(QNetworkProxy::NoProxy);
    m_tor.stop();
    emit changed();
}

void SharedListController::setReport(bool busy, const QString &status, bool ok)
{
    m_reportBusy = busy;
    m_reportStatus = status;
    m_reportOk = ok;
    emit changed();
}

} // namespace app::ui
