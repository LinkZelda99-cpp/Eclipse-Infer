#include "voiceclient.h"

#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QtEndian>
#include <cstring>

VoiceClient::VoiceClient(QObject *parent)
    : QObject(parent), m_player(), m_audioOutput(), m_playbackBuffer()
{
    m_player.setAudioOutput(&m_audioOutput);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
        if (state != QMediaPlayer::PlayingState) emit speakingChanged(false);
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &message) {
        if (!message.isEmpty()) emit errorOccurred(QStringLiteral("Audio playback failed: %1").arg(message));
    });
}

void VoiceClient::setRecognitionProvider(const QString &provider, const QString &apiKey)
{
    m_recognitionProvider = provider;
    m_recognitionApiKey = apiKey.trimmed();
}

void VoiceClient::setSynthesisProvider(const QString &provider, const QString &apiKey, const QString &voiceId)
{
    m_synthesisProvider = provider;
    m_synthesisApiKey = apiKey.trimmed();
    m_voiceId = voiceId.trimmed();
    if (m_voiceId.isEmpty()) m_voiceId = m_synthesisProvider == QStringLiteral("ElevenLabs") ? QStringLiteral("21m00Tcm4TlvDq8ikWAM") : QStringLiteral("alloy");
}
void VoiceClient::fetchVoices(const QString &provider, const QString &apiKey)
{
    if (m_voiceListRequest) {
        m_voiceListRequest->abort();
        m_voiceListRequest->deleteLater();
        m_voiceListRequest = nullptr;
    }

    if (provider == QStringLiteral("OpenAI")) {
        const QStringList voices = {
            QStringLiteral("Alloy"), QStringLiteral("Ash"), QStringLiteral("Ballad"),
            QStringLiteral("Coral"), QStringLiteral("Echo"), QStringLiteral("Fable"),
            QStringLiteral("Nova"), QStringLiteral("Onyx"), QStringLiteral("Sage"),
            QStringLiteral("Shimmer"), QStringLiteral("Verse"), QStringLiteral("Marin"),
            QStringLiteral("Cedar")
        };
        QStringList ids;
        for (const QString &name : voices) ids.append(name.toLower());
        emit voiceListReady(voices, ids);
        return;
    }

    if (provider != QStringLiteral("ElevenLabs")) {
        emit voiceListError(QStringLiteral("Voice browsing is not implemented for this provider yet."));
        return;
    }
    if (apiKey.trimmed().isEmpty()) {
        emit voiceListError(QStringLiteral("Enter your ElevenLabs API key in Settings before browsing its voices."));
        return;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://api.elevenlabs.io/v1/voices")));
    request.setRawHeader("xi-api-key", apiKey.trimmed().toUtf8());
    auto *reply = m_network.get(request);
    m_voiceListRequest = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit voiceListError(QStringLiteral("Could not load ElevenLabs voices: %1").arg(QString::fromUtf8(body.left(1000))));
        } else {
            const QJsonObject json = QJsonDocument::fromJson(body).object();
            const QJsonArray voices = json.value(QStringLiteral("voices")).toArray();
            QStringList names;
            QStringList ids;
            for (const QJsonValue &value : voices) {
                const QJsonObject voice = value.toObject();
                const QString name = voice.value(QStringLiteral("name")).toString().trimmed();
                const QString id = voice.value(QStringLiteral("voice_id")).toString().trimmed();
                if (!name.isEmpty() && !id.isEmpty()) {
                    names.append(name);
                    ids.append(id);
                }
            }
            if (names.isEmpty()) emit voiceListError(QStringLiteral("ElevenLabs returned no available voices for this account."));
            else emit voiceListReady(names, ids);
        }
        if (m_voiceListRequest == reply) m_voiceListRequest = nullptr;
        reply->deleteLater();
    });
}

void VoiceClient::previewVoice(const QString &provider, const QString &apiKey, const QString &voiceId)
{
    const QString oldProvider = m_synthesisProvider;
    const QString oldKey = m_synthesisApiKey;
    const QString oldVoice = m_voiceId;
    setSynthesisProvider(provider, apiKey, voiceId);
    speak(QStringLiteral("Hello! This is a preview of the selected voice in Eclipse Orbit."));
    setSynthesisProvider(oldProvider, oldKey, oldVoice);
}

bool VoiceClient::isRecording() const { return m_recording; }

void VoiceClient::startRecording()
{
    if (m_recording || m_recognitionApiKey.isEmpty()) {
        if (m_recognitionApiKey.isEmpty()) emit errorOccurred(QStringLiteral("Add an API key for the selected speech-recognition provider in Settings."));
        return;
    }
    const QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        emit errorOccurred(QStringLiteral("No microphone was found. Connect a microphone and check Windows microphone permissions."));
        return;
    }
    m_format = QAudioFormat{};
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(m_format)) {
        emit errorOccurred(QStringLiteral("This microphone does not support 16 kHz mono PCM capture, which voice transcription currently requires."));
        return;
    }
    m_pcm.clear();
    m_audioSource = new QAudioSource(device, m_format, this);
    m_audioInput = m_audioSource->start();
    if (!m_audioInput) {
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
        emit errorOccurred(QStringLiteral("Could not start microphone capture."));
        return;
    }
    connect(m_audioInput, &QIODevice::readyRead, this, [this]() {
        if (m_audioInput) m_pcm.append(m_audioInput->readAll());
        constexpr qsizetype maxRecordingBytes = 25 * 1024 * 1024;
        if (m_pcm.size() > maxRecordingBytes) {
            emit errorOccurred(QStringLiteral("Recording reached the 25 MiB limit. Stop recording to transcribe it."));
            stopRecordingAndTranscribe();
        }
    });
    m_recording = true;
    emit recordingChanged(true);
}

QByteArray VoiceClient::wavData() const
{
    QByteArray wav;
    wav.reserve(m_pcm.size() + 44);
    wav.append("RIFF", 4);
    auto put32 = [&wav](quint32 v) { char b[4]; qToLittleEndian(v, reinterpret_cast<uchar *>(b)); wav.append(b, 4); };
    auto put16 = [&wav](quint16 v) { char b[2]; qToLittleEndian(v, reinterpret_cast<uchar *>(b)); wav.append(b, 2); };
    put32(static_cast<quint32>(36 + m_pcm.size()));
    wav.append("WAVEfmt ", 8);
    put32(16);
    put16(1);
    put16(static_cast<quint16>(m_format.channelCount()));
    put32(static_cast<quint32>(m_format.sampleRate()));
    const quint32 byteRate = static_cast<quint32>(m_format.sampleRate() * m_format.channelCount() * m_format.bytesPerSample());
    put32(byteRate);
    put16(static_cast<quint16>(m_format.channelCount() * m_format.bytesPerSample()));
    put16(static_cast<quint16>(m_format.bytesPerSample() * 8));
    wav.append("data", 4);
    put32(static_cast<quint32>(m_pcm.size()));
    wav.append(m_pcm);
    return wav;
}

void VoiceClient::stopRecordingAndTranscribe()
{
    if (!m_recording) return;
    m_recording = false;
    if (m_audioSource) {
        m_audioSource->stop();
        if (m_audioInput) {
            m_pcm.append(m_audioInput->readAll());
            m_audioInput->deleteLater();
        }
        m_audioInput = nullptr;
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }
    emit recordingChanged(false);
    if (m_pcm.isEmpty()) {
        emit errorOccurred(QStringLiteral("No audio was captured."));
        return;
    }

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType, this);
    const bool elevenLabs = m_recognitionProvider == QStringLiteral("ElevenLabs");
    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(elevenLabs ? QStringLiteral("form-data; name=\"model_id\"") : QStringLiteral("form-data; name=\"model\"")));
    modelPart.setBody(elevenLabs ? QByteArray("scribe_v1") : QByteArray("gpt-4o-mini-transcribe"));
    multipart->append(modelPart);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"recording.wav\"")));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setBody(wavData());
    multipart->append(filePart);
    QNetworkRequest request(QUrl(elevenLabs ? QStringLiteral("https://api.elevenlabs.io/v1/speech-to-text") : QStringLiteral("https://api.openai.com/v1/audio/transcriptions")));
    if (elevenLabs) request.setRawHeader("xi-api-key", m_recognitionApiKey.toUtf8());
    else request.setRawHeader("Authorization", QByteArray("Bearer ") + m_recognitionApiKey.toUtf8());
    auto *reply = m_network.post(request, multipart);
    multipart->setParent(reply);
    m_request = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("Speech recognition failed: %1").arg(QString::fromUtf8(body.left(1200))));
        } else {
            const QJsonObject json = QJsonDocument::fromJson(body).object();
            const QString text = json.value(QStringLiteral("text")).toString().trimmed();
            if (text.isEmpty()) emit errorOccurred(QStringLiteral("Speech recognition returned no transcript."));
            else emit transcriptReady(text);
        }
        if (m_request == reply) m_request = nullptr;
        reply->deleteLater();
    });
}

void VoiceClient::speak(const QString &text, const QString &voice)
{
    if (text.trimmed().isEmpty()) return;
    if (m_synthesisApiKey.isEmpty()) {
        emit errorOccurred(QStringLiteral("Add an API key for the selected text-to-speech provider in Settings."));
        return;
    }
    stopSpeaking();
    const bool elevenLabs = m_synthesisProvider == QStringLiteral("ElevenLabs");
    QJsonObject payload;
    if (elevenLabs) {
        payload.insert(QStringLiteral("text"), text.left(4000));
        payload.insert(QStringLiteral("model_id"), QStringLiteral("eleven_multilingual_v2"));
        payload.insert(QStringLiteral("voice_settings"), QJsonObject{
            {QStringLiteral("stability"), 0.5},
            {QStringLiteral("similarity_boost"), 0.75}
        });
    } else {
        payload = QJsonObject{
            {QStringLiteral("model"), QStringLiteral("gpt-4o-mini-tts")},
            {QStringLiteral("voice"), m_voiceId.isEmpty() ? voice : m_voiceId},
            {QStringLiteral("input"), text.left(4000)},
            {QStringLiteral("instructions"), QStringLiteral("Speak naturally and clearly, with warm conversational pacing and subtle expressive intonation.")},
            {QStringLiteral("response_format"), QStringLiteral("mp3")}
        };
    }
    const QUrl url = elevenLabs
        ? QUrl(QStringLiteral("https://api.elevenlabs.io/v1/text-to-speech/%1?output_format=mp3_44100_128").arg(m_voiceId))
        : QUrl(QStringLiteral("https://api.openai.com/v1/audio/speech"));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    if (elevenLabs) request.setRawHeader("xi-api-key", m_synthesisApiKey.toUtf8());
    else request.setRawHeader("Authorization", QByteArray("Bearer ") + m_synthesisApiKey.toUtf8());
    auto *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    m_request = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(QStringLiteral("Voice generation failed: %1").arg(QString::fromUtf8(body.left(1200))));
        } else {
            m_playbackBuffer.close();
            m_playbackBuffer.setData(body);
            if (m_playbackBuffer.open(QIODevice::ReadOnly)) {
                m_player.setSourceDevice(&m_playbackBuffer, QUrl(QStringLiteral("audio.mp3")));
                m_player.play();
                emit speakingChanged(true);
            } else {
                emit errorOccurred(QStringLiteral("Could not prepare generated audio for playback."));
            }
        }
        if (m_request == reply) m_request = nullptr;
        reply->deleteLater();
    });
}

void VoiceClient::stopSpeaking()
{
    m_player.stop();
    m_playbackBuffer.close();
    emit speakingChanged(false);
}
