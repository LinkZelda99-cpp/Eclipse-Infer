#pragma once
#include <QObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>

class ProviderClient final : public QObject {
    Q_OBJECT
public:
    explicit ProviderClient(QObject *parent = nullptr);
    void configure(const QString &provider, const QString &endpoint, const QString &apiKey);
    void refreshModels();
    void chat(const QString &model, const QJsonArray &messages, bool thinking, double temperature, int contextLength);
    void cancel();
    static QString defaultEndpoint(const QString &provider);
    static QStringList providers();
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
    QString apiUrl(const QString &path) const;
    QString m_provider{"Ollama (local)"};
    QString m_endpoint{"http://127.0.0.1:11434"};
    QString m_apiKey;
    QNetworkAccessManager m_network;
    QNetworkReply *m_reply{nullptr};
    QByteArray m_buffer;
    bool m_hadError{false};
};
