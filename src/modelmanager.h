#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QPointer>
#include <QTimer>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

class ModelManagerDialog final : public QDialog {
    Q_OBJECT
public:
    explicit ModelManagerDialog(QWidget *parent = nullptr);

private:
    void refreshModels();
    void pullModel();
    void deleteModel();
    void renameModel();
    void setBusy(bool busy, const QString &message = {});
    void handlePullData();
    void finishPull();
    QString selectedModel() const;

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_pullReply;
    QTimer m_pullInactivityTimer;
    QByteArray m_pullBuffer;
    QTreeWidget *m_models{};
    QLineEdit *m_modelName{};
    QComboBox *m_popularModels{};
    QLineEdit *m_renameName{};
    QProgressBar *m_progress{};
    QLabel *m_status{};
    QPushButton *m_pullButton{};
    QPushButton *m_deleteButton{};
    QPushButton *m_renameButton{};
    QPushButton *m_refreshButton{};
    bool m_pullSucceeded{false};
    bool m_pullTimedOut{false};
};
