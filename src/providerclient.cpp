#include "providerclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

ProviderClient::ProviderClient(QObject *parent) : QObject(parent) {}

QStringList ProviderClient::providers()
{
    return {"Ollama (local)", "OpenAI", "OpenRouter", "Groq", "Together AI",
            "DeepSeek", "Mistral", "LM Studio (local)", "llama.cpp (local)",
            "vLLM (local)", "LocalAI", "Custom OpenAI-compatible"};
}

QString ProviderClient::defaultEndpoint(const QString &provider)
{
    if (provider == "Ollama (local)") return "http://127.0.0.1:11434";
    if (provider == "OpenAI") return "https://api.openai.com/v1";
    if (provider == "OpenRouter") return "https://openrouter.ai/api/v1";
    if (provider == "Groq") return "https://api.groq.com/openai/v1";
    if (provider == "Together AI") return "https://api.together.xyz/v1";
    if (provider == "DeepSeek") return "https://api.deepseek.com/v1";
    if (provider == "Mistral") return "https://api.mistral.ai/v1";
    if (provider == "LM Studio (local)") return "http://127.0.0.1:1234/v1";
    if (provider == "llama.cpp (local)") return "http://127.0.0.1:8080/v1";
    if (provider == "vLLM (local)") return "http://127.0.0.1:8000/v1";
    if (provider == "LocalAI") return "http://127.0.0.1:8080/v1";
    return "https://api.openai.com/v1";
}

void ProviderClient::configure(const QString &provider, const QString &endpoint, const QString &apiKey)
{
    if (m_reply) cancel();
    m_provider = provider;
    m_endpoint = endpoint.trimmed();
    while (m_endpoint.endsWith('/')) m_endpoint.chop(1);
    m_apiKey = apiKey.trimmed();
}

QString ProviderClient::apiUrl(const QString &path) const
{
    return m_endpoint + path;
}

void ProviderClient::refreshModels()
{
    const bool ollama = m_provider == "Ollama (local)";
    QNetworkRequest request(QUrl(apiUrl(ollama ? "/api/tags" : "/models")));
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!ollama && !m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    if (m_provider == "OpenRouter")
        request.setRawHeader("HTTP-Referer", "https://austinmcdonald.dev");
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ollama]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit connectionChanged(false, QString("Connection failed: %1").arg(reply->errorString()));
            reply->deleteLater();
            return;
        }
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            emit connectionChanged(false, "Provider returned an invalid JSON response.");
            reply->deleteLater();
            return;
        }
        const QJsonObject root = doc.object();
        QStringList names;
        const QJsonArray models = root.value(ollama ? "models" : "data").toArray();
        for (const auto &value : models) {
            const QJsonObject item = value.toObject();
            const QString name = item.value(ollama ? "name" : "id").toString();
            if (!name.isEmpty()) names << name;
        }
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        emit modelsReceived(names);
        emit connectionChanged(true, QString("Connected to %1").arg(m_provider));
        reply->deleteLater();
    });
}

void ProviderClient::chat(const QString &model, const QJsonArray &messages, bool thinking, double temperature, int contextLength)
{
    if (m_reply) { emit errorOccurred("A response is already running."); return; }
    const bool ollama = m_provider == "Ollama (local)";
    QJsonObject payload;
    payload.insert("model", model);
    payload.insert("messages", messages);
    payload.insert("stream", true);
    payload.insert("temperature", temperature);
    if (ollama) {
        QJsonObject options;
        options.insert("temperature", temperature);
        options.insert("num_ctx", contextLength);
        payload.insert("think", thinking);
        payload.insert("options", options);
    } else {
        payload.insert("max_tokens", contextLength > 0 ? qMin(contextLength, 8192) : 4096);
        if (thinking) payload.insert("reasoning_effort", "medium");
    }

    QNetworkRequest request(QUrl(apiUrl(ollama ? "/api/chat" : "/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(0);
    if (!ollama && !m_apiKey.isEmpty())
        request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    if (m_provider == "OpenRouter") {
        request.setRawHeader("HTTP-Referer", "https://austinmcdonald.dev");
        request.setRawHeader("X-Title", "Eclipse Infer");
    }
    m_buffer.clear();
    m_hadError = false;
    m_reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    connect(m_reply, &QIODevice::readyRead, this, &ProviderClient::readStream);
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

void ProviderClient::cancel() { if (m_reply) m_reply->abort(); }

void ProviderClient::readStream()
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

void ProviderClient::processLine(const QByteArray &line)
{
    if (line.startsWith("data:")) {
        QByteArray data = line.mid(5).trimmed();
        if (data == "[DONE]") return;
        const auto doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) return;
        const QJsonObject root = doc.object();
        if (root.contains("error")) {
            m_hadError = true;
            const QJsonObject error = root.value("error").toObject();
            emit errorOccurred(error.value("message").toString("Provider returned an error."));
            return;
        }
        const QJsonObject choice = root.value("choices").toArray().value(0).toObject();
        const QJsonObject delta = choice.value("delta").toObject();
        const QString reasoning = delta.value("reasoning_content").toString();
        const QString content = delta.value("content").toString();
        if (!reasoning.isEmpty()) emit thinkingReceived(reasoning);
        if (!content.isEmpty()) emit tokenReceived(content);
        return;
    }
    const auto doc = QJsonDocument::fromJson(line);
    if (!doc.isObject()) return;
    const QJsonObject root = doc.object();
    if (root.contains("error")) {
        m_hadError = true;
        emit errorOccurred(root.value("error").toObject().value("message").toString(root.value("error").toString()));
        return;
    }
    const QJsonObject message = root.value("message").toObject();
    const QString thinking = message.value("thinking").toString();
    const QString content = message.value("content").toString();
    if (!thinking.isEmpty()) emit thinkingReceived(thinking);
    if (!content.isEmpty()) emit tokenReceived(content);
}
