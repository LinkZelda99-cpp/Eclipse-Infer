#pragma once
#include "ollamaclient.h"
#include <QJsonArray>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>
class QComboBox; class QLineEdit; class QPushButton; class QScrollArea; class QSlider;
class QSpinBox; class QTextBrowser; class QVBoxLayout; class QLabel; class QCheckBox;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
private:
    struct Message { QString role; QString content; QString thinking; };
    void buildUi();
    void applyTheme(const QString &theme);
    void refreshModels();
    void sendMessage();
    void addMessageCard(const QString &role, const QString &content, bool streaming = false, const QString &thinking = QString{});
    void newChat();
    void rebuildHistory();
    void showWelcome();
    void saveCurrentConversation();
    void openConversation(const QString &id);
    void showSettings();
    QJsonArray apiMessages() const;
    OllamaClient m_ollama;
    QComboBox *m_models{}; QLineEdit *m_input{}; QPushButton *m_send{}; QPushButton *m_codeModeButton{};
    QLabel *m_status{}; QLabel *m_chatTitle{};
    QScrollArea *m_scroll{}; QWidget *m_messageHost{}; QVBoxLayout *m_messageLayout{};
    QSlider *m_temperature{}; QCheckBox *m_thinking{};
    QVBoxLayout *m_historyLayout{}; QPointer<QTextBrowser> m_streamView; QPointer<QTextBrowser> m_streamThinkingView; QPointer<QPushButton> m_streamThinkingLabel;
    QList<Message> m_messages;
    QString m_title;
    QString m_conversationId;
    QStringList m_historyIds;
    bool m_dark{true}; bool m_busy{false}; bool m_codeMode{false};
};
