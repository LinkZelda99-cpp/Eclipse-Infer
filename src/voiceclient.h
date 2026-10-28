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

class VoiceClient final : public QObject {
    Q_OBJECT
public:
    explicit VoiceClient(QObject *parent = nullptr);
    void setApiKey(const QString &key);
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

private:
    QByteArray wavData() const;
    void appendWavHeader(QByteArray &wav) const;
    QString m_apiKey;
    QAudioSource *m_audioSource{};
    QIODevice *m_audioInput{};
    QAudioFormat m_format;
    QByteArray m_pcm;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_request;
    QMediaPlayer m_player;
    QAudioOutput m_audioOutput;
    QBuffer m_playbackBuffer;
    bool m_recording{false};
};
