#include "providerclient.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

ProviderClient::ProviderClient(QObject *parent) : QObject(parent) {}

QStringList ProviderClient::providers()
{
    return {"Ollama (local)", "OpenAI", "OpenRouter", "Groq", "Together AI",
            "DeepSeek", "Mistral", "Anthropic", "Google Gemini", "LM Studio (local)", "llama.cpp (local)",
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
    if (provider == "Anthropic") return "https://api.anthropic.com/v1";
    if (provider == "Google Gemini") return "https://generativelanguage.googleapis.com/v1beta";
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
    const bool gemini = m_provider == "Google Gemini";
    const bool anthropic = m_provider == "Anthropic";
    QNetworkRequest request(QUrl(apiUrl(ollama ? "/api/tags" : gemini ? "/models?pageSize=100" : "/models")));
    request.setTransferTimeout(10000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    if (!ollama && !m_apiKey.isEmpty()) {
        if (anthropic) {
            request.setRawHeader("x-api-key", m_apiKey.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (gemini) request.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
        else request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    }
    if (m_provider == "OpenRouter")
        request.setRawHeader("HTTP-Referer", "https://austinmcdonald.dev");
    const QString requestedProvider = m_provider;
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ollama, gemini, requestedProvider]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            if (requestedProvider == m_provider)
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
        const QJsonArray models = root.value(ollama ? "models" : gemini ? "models" : "data").toArray();
        for (const auto &value : models) {
            const QJsonObject item = value.toObject();
            QString name = item.value(ollama ? "name" : gemini ? "name" : "id").toString();
            if (gemini && name.startsWith("models/")) name.remove(0, 7);
            if (!name.isEmpty()) names << name;
        }
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        emit modelsReceivedForProvider(requestedProvider, names);
        emit modelsReceived(names);
        if (requestedProvider == m_provider)
            emit connectionChanged(true, QString("Connected to %1").arg(requestedProvider));
        reply->deleteLater();
    });
}

void ProviderClient::chat(const QString &model, const QJsonArray &messages, bool thinking, double temperature, int contextLength)
{
    if (m_reply) { emit errorOccurred("A response is already running."); return; }
    const bool ollama = m_provider == "Ollama (local)";
    const bool anthropic = m_provider == "Anthropic";
    const bool gemini = m_provider == "Google Gemini";
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
    } else if (anthropic) {
        QJsonArray nativeMessages;
        QString systemPrompt;
        for (const auto &entry : messages) {
            const QJsonObject item = entry.toObject();
            if (item.value("role").toString() == "system") systemPrompt += item.value("content").toString() + "\n";
            else {
                QJsonObject nativeItem;
                nativeItem.insert("role", item.value("role").toString() == "assistant" ? "assistant" : "user");
                nativeItem.insert("content", item.value("content").toString());
                nativeMessages.append(nativeItem);
            }
        }
        payload.insert("messages", nativeMessages);
        payload.insert("max_tokens", 4096);
        if (!systemPrompt.isEmpty()) payload.insert("system", systemPrompt.trimmed());
        payload.insert("stream", true);
        payload.remove("temperature");
    } else if (gemini) {
        QJsonArray contents;
        QString systemPrompt;
        for (const auto &entry : messages) {
            const QJsonObject item = entry.toObject();
            const QString role = item.value("role").toString();
            if (role == "system") { systemPrompt += item.value("content").toString() + "\n"; continue; }
            QJsonObject content;
            content.insert("role", role == "assistant" ? "model" : "user");
            QJsonObject part; part.insert("text", item.value("content").toString());
            content.insert("parts", QJsonArray{part}); contents.append(content);
        }
        payload = QJsonObject{};
        payload.insert("contents", contents);
        QJsonObject generation; generation.insert("temperature", temperature);
        generation.insert("maxOutputTokens", 4096);
        payload.insert("generationConfig", generation);
        if (!systemPrompt.isEmpty()) {
            QJsonObject system; system.insert("parts", QJsonArray{QJsonObject{{"text", systemPrompt.trimmed()}}});
            payload.insert("systemInstruction", system);
        }
    } else {
        if (m_provider == "OpenAI") payload.insert("max_completion_tokens", 4096);
        else payload.insert("max_tokens", 4096);
        if (thinking && (m_provider == "OpenAI" || m_provider == "OpenRouter"))
            payload.insert("reasoning_effort", "medium");
    }

    QString chatPath = ollama ? "/api/chat" : anthropic ? "/messages" : gemini ? QString("/models/%1:streamGenerateContent?alt=sse").arg(model) : "/chat/completions";
    QNetworkRequest request(QUrl(apiUrl(chatPath)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(0);
    if (!ollama && !m_apiKey.isEmpty()) {
        if (anthropic) {
            request.setRawHeader("x-api-key", m_apiKey.toUtf8());
            request.setRawHeader("anthropic-version", "2023-06-01");
        } else if (gemini) request.setRawHeader("x-goog-api-key", m_apiKey.toUtf8());
        else request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
    }
    if (m_provider == "OpenRouter") {
        request.setRawHeader("HTTP-Referer", "https://austinmcdonald.dev");
        request.setRawHeader("X-Title", "Eclipse Orbit");
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
        if (m_provider == "Anthropic") {
            const QString type = root.value("type").toString();
            const QJsonObject delta = root.value("delta").toObject();
            if (type == "content_block_delta") {
                if (delta.value("type").toString() == "thinking_delta") emit thinkingReceived(delta.value("thinking").toString());
                else emit tokenReceived(delta.value("text").toString());
            } else if (type == "error") {
                m_hadError = true;
                emit errorOccurred(root.value("error").toObject().value("message").toString("Anthropic API error."));
            }
            return;
        }
        if (m_provider == "Google Gemini") {
            const QJsonArray candidates = root.value("candidates").toArray();
            if (!candidates.isEmpty()) {
                const QJsonArray parts = candidates.first().toObject().value("content").toObject().value("parts").toArray();
                for (const auto &partValue : parts) {
                    const QString text = partValue.toObject().value("text").toString();
                    if (!text.isEmpty()) emit tokenReceived(text);
                }
            }
            if (root.contains("error")) {
                m_hadError = true;
                emit errorOccurred(root.value("error").toObject().value("message").toString("Gemini API error."));
            }
            return;
        }
        if (root.contains("error")) {
            m_hadError = true;
            const QJsonObject error = root.value("error").toObject();
            emit errorOccurred(error.value("message").toString("Provider returned an error."));
            return;
        }
        const QJsonArray choices = root.value("choices").toArray();
        const QJsonObject choice = choices.isEmpty() ? QJsonObject{} : choices.at(0).toObject();
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
