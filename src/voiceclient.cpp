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

void VoiceClient::setApiKey(const QString &key) { m_apiKey = key.trimmed(); }
bool VoiceClient::isRecording() const { return m_recording; }

void VoiceClient::startRecording()
{
    if (m_recording || m_apiKey.isEmpty()) {
        if (m_apiKey.isEmpty()) emit errorOccurred(QStringLiteral("Voice APIs need an OpenAI API key. Add it in Settings first."));
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
        m_format = device.preferredFormat();
        if (m_format.sampleFormat() == QAudioFormat::Unknown) {
            emit errorOccurred(QStringLiteral("The microphone does not expose a supported audio format."));
            return;
        }
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
    QHttpPart modelPart;
    modelPart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(QStringLiteral("form-data; name=\"model\"")));
    modelPart.setBody("gpt-4o-mini-transcribe");
    multipart->append(modelPart);
    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader, QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"recording.wav\"")));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, QVariant(QStringLiteral("audio/wav")));
    filePart.setBody(wavData());
    multipart->append(filePart);
    QNetworkRequest request(QUrl(QStringLiteral("https://api.openai.com/v1/audio/transcriptions")));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
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
    if (m_apiKey.isEmpty()) {
        emit errorOccurred(QStringLiteral("Voice APIs need an OpenAI API key. Add it in Settings first."));
        return;
    }
    stopSpeaking();
    QJsonObject payload{
        {QStringLiteral("model"), QStringLiteral("gpt-4o-mini-tts")},
        {QStringLiteral("voice"), voice},
        {QStringLiteral("input"), text.left(4000)},
        {QStringLiteral("instructions"), QStringLiteral("Speak naturally and clearly, with warm conversational pacing and subtle expressive intonation.")},
        {QStringLiteral("response_format"), QStringLiteral("mp3")}
    };
    QNetworkRequest request(QUrl(QStringLiteral("https://api.openai.com/v1/audio/speech")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + m_apiKey.toUtf8());
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
