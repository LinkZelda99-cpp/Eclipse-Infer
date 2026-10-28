#pragma once

#include <QObject>
#include <QAudioSource>
#include <QAudioFormat>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QProcess>

class VoiceClient final : public QObject {
    Q_OBJECT
public:
    explicit VoiceClient(QObject *parent = nullptr);
    void setRecognitionProvider(const QString &provider, const QString &apiKey);
    void setSynthesisProvider(const QString &provider, const QString &apiKey, const QString &voiceId);
    void fetchVoices(const QString &provider, const QString &apiKey);
    void previewVoice(const QString &provider, const QString &apiKey, const QString &voiceId);
    bool isRecording() const;
    void startRecording();
    void stopRecordingAndTranscribe();
    void speak(const QString &text, const QString &voice = QStringLiteral("alloy"));
    void stopSpeaking();

signals:
    void recordingChanged(bool recording);
    void transcriptReady(const QString &text);
    void speakingChanged(bool speaking);
    void errorOccurred(const QString &message);
    void voiceListReady(const QStringList &names, const QStringList &ids);
    void voiceListError(const QString &message);

private:
    QByteArray wavData() const;
    void appendWavHeader(QByteArray &wav) const;
    QString m_recognitionProvider{"OpenAI"};
    QString m_synthesisProvider{"OpenAI"};
    QString m_recognitionApiKey;
    QString m_synthesisApiKey;
    QString m_voiceId{"alloy"};
    QAudioSource *m_audioSource{};
    QIODevice *m_audioInput{};
    QAudioFormat m_format;
    QByteArray m_pcm;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_request;
    QPointer<QNetworkReply> m_voiceListRequest;
    QProcess m_localSpeechProcess;
    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QBuffer m_playbackBuffer;
    bool m_recording{false};
};
