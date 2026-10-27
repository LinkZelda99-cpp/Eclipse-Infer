#include "ollamaclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
OllamaClient::OllamaClient(QObject *parent) : QObject(parent) {}
void OllamaClient::refreshModels()
{
    QNetworkRequest request(QUrl("http://127.0.0.1:11434/api/tags"));
    request.setTransferTimeout(5000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const auto body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectionChanged(false, "Ollama is unavailable. Start Ollama and refresh.");
            reply->deleteLater();
            return;
        }
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            emit connectionChanged(false, "Ollama returned an invalid response.");
            reply->deleteLater();
            return;
        }
        QStringList names;
        for (const auto &v : doc.object().value("models").toArray()) {
            const QString name = v.toObject().value("name").toString();
            if (!name.isEmpty()) names << name;
        }
        names.sort(Qt::CaseInsensitive);
        emit modelsReceived(names);
        emit connectionChanged(true, "Connected to Ollama");
        reply->deleteLater();
    });
}
void OllamaClient::chat(const QString &model, const QJsonArray &messages, bool thinking, double temperature, int contextLength)
{
    if (m_reply) { emit errorOccurred("A response is already running."); return; }
    QJsonObject options;
    options.insert("temperature", temperature);
    options.insert("num_ctx", contextLength);
    QJsonObject payload;
    payload.insert("model", model);
    payload.insert("messages", messages);
    payload.insert("stream", true);
    payload.insert("think", thinking);
    payload.insert("options", options);
    QNetworkRequest request(QUrl("http://127.0.0.1:11434/api/chat"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_buffer.clear();
    m_hadError = false;
    m_reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QIODevice::readyRead, this, &OllamaClient::readStream);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        if (!m_reply) return;
        readStream();
        if (!m_buffer.trimmed().isEmpty()) processLine(m_buffer.trimmed());
        auto *reply = m_reply;
        m_reply = nullptr;
        if (reply->error() != QNetworkReply::NoError && !m_hadError)
            emit errorOccurred(QString("Request failed: %1").arg(reply->errorString()));
        reply->deleteLater();
        emit chatFinished();
    });
    connect(m_reply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError) {
        if (m_reply && m_reply->error() != QNetworkReply::NoError) m_hadError = true;
    });
}
void OllamaClient::cancel() { if (m_reply) m_reply->abort(); }
void OllamaClient::readStream()
{
    if (!m_reply) return;
    m_buffer.append(m_reply->readAll());
    qsizetype newline = -1;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (!line.isEmpty()) processLine(line);
    }
}
void OllamaClient::processLine(const QByteArray &line)
{
    const auto doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) return;
    const auto obj = doc.object();
    if (obj.contains("error")) {
        m_hadError = true;
        emit errorOccurred(obj.value("error").toString());
        return;
    }
    const auto message = obj.value("message").toObject();
    const QString thinking = message.value("thinking").toString();
    const QString content = message.value("content").toString();
    if (!thinking.isEmpty()) emit thinkingReceived(thinking);
    if (!content.isEmpty()) emit tokenReceived(content);
}
