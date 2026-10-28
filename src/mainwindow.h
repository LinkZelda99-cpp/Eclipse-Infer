#pragma once
#include "providerclient.h"
#include <QJsonArray>
#include <QMainWindow>
#include <QCloseEvent>
#include <QPointer>
#include <QStringList>
class UpdateManager;
class QComboBox; class QLineEdit; class QPushButton; class QScrollArea; class QSlider;
class QSpinBox; class QTextBrowser; class QTextEdit; class QVBoxLayout; class QLabel; class QCheckBox; class QSystemTrayIcon;
class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
protected:
    void closeEvent(QCloseEvent *event) override;
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
    void showModelManager();
    void checkForUpdates(bool userInitiated = false);
    void installDownloadedUpdate(const QString &path, const QString &version);
    QJsonArray apiMessages() const;
    ProviderClient m_providerClient;
    UpdateManager *m_updateManager{};
    QSystemTrayIcon *m_trayIcon{};
    QComboBox *m_provider{}; QComboBox *m_models{}; QTextEdit *m_input{}; QPushButton *m_send{}; QPushButton *m_codeModeButton{};
    QLabel *m_status{}; QLabel *m_chatTitle{}; QLabel *m_logoLabel{}; QPointer<QLabel> m_welcomeLogo;
    QScrollArea *m_scroll{}; QWidget *m_messageHost{}; QVBoxLayout *m_messageLayout{};
    QSlider *m_temperature{}; QCheckBox *m_thinking{}; QSpinBox *m_contextLength{}; QPushButton *m_stop{};
    QVBoxLayout *m_historyLayout{}; QPointer<QTextBrowser> m_streamView; QPointer<QTextBrowser> m_streamThinkingView; QPointer<QPushButton> m_streamThinkingLabel;
    QList<Message> m_messages;
    QString m_title;
    QString m_conversationId;
    QStringList m_historyIds;
    bool m_dark{true}; bool m_busy{false}; bool m_codeMode{false};
};
