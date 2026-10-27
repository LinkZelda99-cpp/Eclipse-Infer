#include "updater.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QVersionNumber>

namespace {
const QUrl latestReleaseApi(QStringLiteral("https://api.github.com/repos/LinkZelda99-cpp/Eclipse-Infer/releases/latest"));
QString normalizedVersion(QString value) {
    value = value.trimmed();
    if (value.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        value.remove(0, 1);
    return value;
}
}

UpdateManager::UpdateManager(QObject *parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void UpdateManager::checkForUpdates(bool userInitiated) {
    m_userInitiated = userInitiated;
    QNetworkRequest request(latestReleaseApi);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eclipse-Infer-Updater"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError) {
            emit checkFailed(QStringLiteral("Couldn't check GitHub Releases: %1").arg(errorText), m_userInitiated);
            return;
        }
        QJsonParseError parseError{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit checkFailed(QStringLiteral("GitHub returned an invalid release response."), m_userInitiated);
            return;
        }
        const QJsonObject release = document.object();
        if (release.value(QStringLiteral("draft")).toBool() || release.value(QStringLiteral("prerelease")).toBool()) {
            emit checkFailed(QStringLiteral("The latest GitHub release isn't a stable public release."), m_userInitiated);
            return;
        }
        const QString latest = normalizedVersion(release.value(QStringLiteral("tag_name")).toString());
        const QString current = normalizedVersion(QCoreApplication::applicationVersion());
        if (latest.isEmpty()) {
            emit checkFailed(QStringLiteral("The GitHub release has no version tag."), m_userInitiated);
            return;
        }
        const QVersionNumber latestVersion = QVersionNumber::fromString(latest);
        const QVersionNumber currentVersion = QVersionNumber::fromString(current);
        if (latestVersion.isNull() || currentVersion.isNull()) {
            emit checkFailed(QStringLiteral("Couldn't compare app version '%1' with release '%2'.").arg(current, latest), m_userInitiated);
            return;
        }
        if (QVersionNumber::compare(latestVersion, currentVersion) <= 0) {
            emit upToDate(m_userInitiated);
            return;
        }
        QUrl assetUrl;
        const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QStringLiteral("name")).toString();
#ifdef Q_OS_WIN
            if (name.compare(QStringLiteral("EclipseInfer-Windows-x64.zip"), Qt::CaseInsensitive) == 0) {
                assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
                break;
            }
            if (assetUrl.isEmpty() && name.compare(QStringLiteral("EclipseInfer.exe"), Qt::CaseInsensitive) == 0)
                assetUrl = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
#endif
        }
        emit updateAvailable(latest, release.value(QStringLiteral("body")).toString(),
                             assetUrl, QUrl(release.value(QStringLiteral("html_url")).toString()));
    });
}

void UpdateManager::downloadUpdate(const QUrl &url, const QString &version) {
    if (!url.isValid() || url.scheme() != QStringLiteral("https") ||
        (url.host() != QStringLiteral("github.com") && !url.host().endsWith(QStringLiteral(".githubusercontent.com")))) {
        emit downloadFailed(QStringLiteral("The update download URL isn't a trusted GitHub HTTPS URL."));
        return;
    }
    m_downloadVersion = version;
    m_downloadIsZip = url.path().endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Eclipse-Infer-Updater"));
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateManager::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray data = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        if (error != QNetworkReply::NoError || data.size() < 1024) {
            emit downloadFailed(QStringLiteral("Update download failed: %1").arg(error == QNetworkReply::NoError ? QStringLiteral("the downloaded file was unexpectedly small") : errorText));
            return;
        }
        const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (tempDir.isEmpty()) {
            emit downloadFailed(QStringLiteral("Windows didn't provide a temporary folder for the update."));
            return;
        }
        const QString extension = m_downloadIsZip ? QStringLiteral("zip") : QStringLiteral("exe");
        const QString path = QDir(tempDir).filePath(QStringLiteral("EclipseInfer-update-%1.%2").arg(m_downloadVersion, extension));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            file.close();
            QFile::remove(path);
            emit downloadFailed(QStringLiteral("Couldn't save the downloaded update."));
            return;
        }
        file.close();
        emit downloadReady(path, m_downloadVersion);
    });
}
