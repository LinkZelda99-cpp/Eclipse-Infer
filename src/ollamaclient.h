#pragma once
#include <QObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
class OllamaClient final : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);
    void refreshModels();
    void chat(const QString &model, const QJsonArray &messages, bool thinking, double temperature, int contextLength);
    void cancel();
signals:
    void connectionChanged(bool connected, const QString &message);
    void modelsReceived(const QStringList &models);
    void tokenReceived(const QString &text);
    void thinkingReceived(const QString &text);
    void chatFinished();
    void errorOccurred(const QString &message);
private:
    void readStream();
    void processLine(const QByteArray &line);
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply{nullptr};
    QByteArray m_buffer;
    bool m_hadError{false};
};
