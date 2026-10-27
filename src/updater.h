#pragma once
#include <QObject>
#include <QByteArray>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateManager final : public QObject {
    Q_OBJECT
public:
    explicit UpdateManager(QObject *parent = nullptr);
    void checkForUpdates(bool userInitiated = false);
    void downloadUpdate(const QUrl &url, const QString &version);

signals:
    void upToDate();
    void updateAvailable(const QString &version, const QString &notes, const QUrl &assetUrl, const QUrl &releaseUrl);
    void checkFailed(const QString &message);
    void downloadProgress(qint64 received, qint64 total);
    void downloadReady(const QString &path, const QString &version);
    void downloadFailed(const QString &message);

private:
    QNetworkAccessManager *m_network{};
    bool m_userInitiated{false};
    QString m_downloadVersion;
};
