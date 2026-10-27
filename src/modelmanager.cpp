#include "modelmanager.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QDateTime>
#include <QUrl>

namespace {
const QUrl ollamaUrl(const QString &path) {
    return QUrl(QStringLiteral("http://127.0.0.1:11434") + path);
}
QString humanSize(qint64 bytes) {
    if (bytes <= 0) return QStringLiteral("—");
    const double gib = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);
    if (gib >= 1.0) return QStringLiteral("%1 GiB").arg(gib, 0, 'f', 1);
    const double mib = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QStringLiteral("%1 MiB").arg(mib, 0, 'f', 0);
}
}

ModelManagerDialog::ModelManagerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Model Manager — Eclipse Infer"));
    resize(900, 620);
    setMinimumSize(720, 500);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(12);

    auto *heading = new QLabel(QStringLiteral("Manage local models"), this);
    heading->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
    root->addWidget(heading);
    auto *description = new QLabel(
        QStringLiteral("Browse, download, rename, and remove models installed in Ollama. "
                       "Cloud-provider models are hosted by those services, so Eclipse Infer cannot install or delete them."),
        this);
    description->setWordWrap(true);
    root->addWidget(description);

    m_models = new QTreeWidget(this);
    m_models->setColumnCount(4);
    m_models->setHeaderLabels({QStringLiteral("Model"), QStringLiteral("Size"),
                               QStringLiteral("Family / quantization"), QStringLiteral("Last updated")});
    m_models->setRootIsDecorated(false);
    m_models->setAlternatingRowColors(true);
    m_models->setSelectionMode(QAbstractItemView::SingleSelection);
    m_models->header()->setStretchLastSection(true);
    m_models->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_models->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_models->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    root->addWidget(m_models, 1);

    auto *downloadGroup = new QFormLayout;
    m_modelName = new QLineEdit(this);
    m_modelName->setPlaceholderText(QStringLiteral("e.g. qwen2.5:7b or llama3.2"));
    m_modelName->setClearButtonEnabled(true);
    downloadGroup->addRow(QStringLiteral("Download from the Ollama library"), m_modelName);
    root->addLayout(downloadGroup);

    auto *actions = new QHBoxLayout;
    m_pullButton = new QPushButton(QStringLiteral("Download model"), this);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh list"), this);
    m_deleteButton = new QPushButton(QStringLiteral("Delete selected"), this);
    m_renameButton = new QPushButton(QStringLiteral("Rename selected"), this);
    actions->addWidget(m_pullButton);
    actions->addWidget(m_refreshButton);
    actions->addStretch();
    actions->addWidget(m_renameButton);
    actions->addWidget(m_deleteButton);
    root->addLayout(actions);

    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QStringLiteral("Ready"));
    root->addWidget(m_progress);
    m_status = new QLabel(QStringLiteral("Connecting to Ollama at 127.0.0.1:11434…"), this);
    m_status->setWordWrap(true);
    root->addWidget(m_status);

    auto *closeButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(closeButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(closeButtons);

    connect(m_refreshButton, &QPushButton::clicked, this, &ModelManagerDialog::refreshModels);
    connect(m_pullButton, &QPushButton::clicked, this, &ModelManagerDialog::pullModel);
    connect(m_deleteButton, &QPushButton::clicked, this, &ModelManagerDialog::deleteModel);
    connect(m_renameButton, &QPushButton::clicked, this, &ModelManagerDialog::renameModel);
    connect(m_models, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const bool selected = !selectedModel().isEmpty();
        m_deleteButton->setEnabled(selected);
        m_renameButton->setEnabled(selected);
        if (selected) m_renameName->setText(selectedModel());
    });

    // Keep the rename target as an inline, clearly labelled field.
    m_renameName = new QLineEdit(this);
    m_renameName->setPlaceholderText(QStringLiteral("New name, e.g. my-qwen:7b"));
    auto *renameRow = new QHBoxLayout;
    renameRow->addWidget(new QLabel(QStringLiteral("Rename target"), this));
    renameRow->addWidget(m_renameName, 1);
    root->insertLayout(root->indexOf(m_progress), renameRow);

    setBusy(false);
    refreshModels();
}

QString ModelManagerDialog::selectedModel() const {
    const auto items = m_models->selectedItems();
    return items.isEmpty() ? QString{} : items.first()->data(0, Qt::UserRole).toString();
}

void ModelManagerDialog::setBusy(bool busy, const QString &message) {
    m_pullButton->setEnabled(!busy);
    m_refreshButton->setEnabled(!busy);
    m_deleteButton->setEnabled(!busy && !selectedModel().isEmpty());
    m_renameButton->setEnabled(!busy && !selectedModel().isEmpty());
    m_modelName->setEnabled(!busy);
    m_renameName->setEnabled(!busy);
    if (!message.isEmpty()) m_status->setText(message);
}

void ModelManagerDialog::refreshModels() {
    if (m_pullReply) return;
    m_status->setText(QStringLiteral("Loading installed Ollama models…"));
    m_refreshButton->setEnabled(false);
    QNetworkRequest request(ollamaUrl(QStringLiteral("/api/tags")));
    request.setTransferTimeout(10000);
    auto *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        m_refreshButton->setEnabled(true);
        if (error != QNetworkReply::NoError) {
            m_status->setText(QStringLiteral("Could not reach Ollama: %1. Is Ollama running?").arg(errorText));
            return;
        }
        const auto doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            m_status->setText(QStringLiteral("Ollama returned an unreadable model list."));
            return;
        }
        m_models->clear();
        auto models = doc.object().value(QStringLiteral("models")).toArray();
        for (const auto &value : models) {
            const auto model = value.toObject();
            const auto details = model.value(QStringLiteral("details")).toObject();
            const QString name = model.value(QStringLiteral("name")).toString();
            auto *item = new QTreeWidgetItem(m_models);
            item->setText(0, name);
            item->setData(0, Qt::UserRole, name);
            item->setText(1, humanSize(static_cast<qint64>(model.value(QStringLiteral("size")).toDouble())));
            item->setText(2, QStringLiteral("%1 · %2").arg(
                details.value(QStringLiteral("family")).toString(QStringLiteral("unknown")),
                details.value(QStringLiteral("quantization_level")).toString(QStringLiteral("unknown"))));
            const auto modified = QDateTime::fromString(model.value(QStringLiteral("modified_at")).toString(), Qt::ISODateWithMs);
            item->setText(3, modified.isValid() ? modified.toLocalTime().toString(QStringLiteral("yyyy-MM-dd")) : QStringLiteral("—"));
        }
        m_models->sortItems(0, Qt::AscendingOrder);
        m_status->setText(QStringLiteral("%1 local model(s) found. Models are managed by your local Ollama installation.").arg(models.size()));
        if (models.isEmpty()) m_status->setText(QStringLiteral("No local models installed yet. Enter an Ollama library model name below to download one."));
    });
}

void ModelManagerDialog::pullModel() {
    const QString name = m_modelName->text().trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char(' '))) {
        QMessageBox::information(this, QStringLiteral("Model name needed"),
            QStringLiteral("Enter a valid Ollama library model reference, such as qwen2.5:7b."));
        return;
    }
    const auto answer = QMessageBox::question(this, QStringLiteral("Download model"),
        QStringLiteral("Download “%1” from the Ollama model library?\n\nModel weights can be large and may use substantial disk space and bandwidth. Check the model's license and terms before use.").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;

    m_pullSucceeded = false;
    m_pullBuffer.clear();
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setFormat(QStringLiteral("Starting…"));
    setBusy(true, QStringLiteral("Downloading %1…").arg(name));
    QNetworkRequest request(ollamaUrl(QStringLiteral("/api/pull")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(0);
    m_pullReply = m_network.post(request, QJsonDocument(QJsonObject{{QStringLiteral("model"), name},
        {QStringLiteral("stream"), true}}).toJson(QJsonDocument::Compact));
    connect(m_pullReply, &QIODevice::readyRead, this, &ModelManagerDialog::handlePullData);
    connect(m_pullReply, &QNetworkReply::finished, this, &ModelManagerDialog::finishPull);
}

void ModelManagerDialog::handlePullData() {
    if (!m_pullReply) return;
    m_pullBuffer.append(m_pullReply->readAll());
    qsizetype newline = -1;
    while ((newline = m_pullBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_pullBuffer.left(newline).trimmed();
        m_pullBuffer.remove(0, newline + 1);
        if (line.isEmpty()) continue;
        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) continue;
        const auto obj = doc.object();
        if (obj.contains(QStringLiteral("error"))) {
            m_status->setText(obj.value(QStringLiteral("error")).toString());
            m_pullSucceeded = false;
            continue;
        }
        const QString status = obj.value(QStringLiteral("status")).toString(QStringLiteral("Downloading…"));
        const double total = obj.value(QStringLiteral("total")).toDouble();
        const double completed = obj.value(QStringLiteral("completed")).toDouble();
        if (total > 0) {
            const int percent = qBound(0, qRound((completed / total) * 100.0), 100);
            m_progress->setValue(percent);
            m_progress->setFormat(QStringLiteral("%1% — %2").arg(percent).arg(status));
        } else {
            m_progress->setRange(0, 0);
            m_progress->setFormat(status);
        }
        m_status->setText(status);
        if (status == QStringLiteral("success")) m_pullSucceeded = true;
    }
}

void ModelManagerDialog::finishPull() {
    if (!m_pullReply) return;
    handlePullData();
    if (!m_pullBuffer.trimmed().isEmpty()) {
        const QByteArray last = m_pullBuffer.trimmed();
        const auto doc = QJsonDocument::fromJson(last);
        if (doc.isObject() && doc.object().value(QStringLiteral("status")).toString() == QStringLiteral("success"))
            m_pullSucceeded = true;
        if (doc.isObject() && doc.object().contains(QStringLiteral("error"))) {
            m_status->setText(doc.object().value(QStringLiteral("error")).toString());
            m_pullSucceeded = false;
        }
    }
    auto *reply = m_pullReply.data();
    const auto networkError = reply->error();
    const QString errorText = reply->errorString();
    m_pullReply = nullptr;
    reply->deleteLater();
    setBusy(false);
    m_progress->setRange(0, 100);
    if (networkError != QNetworkReply::NoError || !m_pullSucceeded) {
        m_progress->setFormat(QStringLiteral("Download failed"));
        if (networkError != QNetworkReply::NoError)
            m_status->setText(QStringLiteral("Download failed: %1").arg(errorText));
        else if (m_status->text().isEmpty())
            m_status->setText(QStringLiteral("Download failed. Check the model name and Ollama logs."));
        return;
    }
    m_progress->setValue(100);
    m_progress->setFormat(QStringLiteral("Download complete"));
    m_status->setText(QStringLiteral("Model downloaded. Refreshing the installed-model list…"));
    m_modelName->clear();
    refreshModels();
}

void ModelManagerDialog::deleteModel() {
    const QString name = selectedModel();
    if (name.isEmpty()) return;
    if (QMessageBox::warning(this, QStringLiteral("Delete local model"),
        QStringLiteral("Permanently remove the local model “%1” from Ollama? This does not delete any hosted/cloud model.").arg(name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;

    setBusy(true, QStringLiteral("Deleting %1…").arg(name));
    QNetworkRequest request(ollamaUrl(QStringLiteral("/api/delete")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *reply = m_network.sendCustomRequest(request, "DELETE", QJsonDocument(QJsonObject{{QStringLiteral("model"), name}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, name]() {
        const auto error = reply->error();
        const QString errorText = reply->errorString();
        reply->deleteLater();
        setBusy(false);
        if (error != QNetworkReply::NoError) {
            m_status->setText(QStringLiteral("Delete failed for %1: %2").arg(name, errorText));
            return;
        }
        m_status->setText(QStringLiteral("Deleted %1.").arg(name));
        refreshModels();
    });
}

void ModelManagerDialog::renameModel() {
    const QString source = selectedModel();
    const QString destination = m_renameName->text().trimmed();
    if (source.isEmpty() || destination.isEmpty()) return;
    if (source == destination) {
        m_status->setText(QStringLiteral("The new name is the same as the current name."));
        return;
    }
    if (destination.contains(QLatin1Char(' '))) {
        QMessageBox::information(this, QStringLiteral("Invalid model name"),
            QStringLiteral("Use an Ollama model name such as my-qwen:7b (no spaces)."));
        return;
    }
    if (QMessageBox::question(this, QStringLiteral("Rename model"),
        QStringLiteral("Ollama does not provide an in-place rename. Eclipse Infer will copy “%1” to “%2”, then remove the original only if the copy succeeds.").arg(source, destination),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;

    setBusy(true, QStringLiteral("Copying model to %1…").arg(destination));
    QNetworkRequest request(ollamaUrl(QStringLiteral("/api/copy")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    auto *copyReply = m_network.post(request, QJsonDocument(QJsonObject{
        {QStringLiteral("source"), source}, {QStringLiteral("destination"), destination}
    }).toJson(QJsonDocument::Compact));
    connect(copyReply, &QNetworkReply::finished, this, [this, copyReply, source, destination]() {
        const auto error = copyReply->error();
        const QString errorText = copyReply->errorString();
        copyReply->deleteLater();
        if (error != QNetworkReply::NoError) {
            setBusy(false, QStringLiteral("Copy failed; original model was kept: %1").arg(errorText));
            return;
        }
        setBusy(true, QStringLiteral("Copy succeeded. Removing original model…"));
        QNetworkRequest deleteRequest(ollamaUrl(QStringLiteral("/api/delete")));
        deleteRequest.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        auto *deleteReply = m_network.sendCustomRequest(deleteRequest, "DELETE", QJsonDocument(QJsonObject{{QStringLiteral("model"), source}}).toJson(QJsonDocument::Compact));
        connect(deleteReply, &QNetworkReply::finished, this, [this, deleteReply, source, destination]() {
            const auto deleteError = deleteReply->error();
            const QString deleteErrorText = deleteReply->errorString();
            deleteReply->deleteLater();
            setBusy(false);
            if (deleteError != QNetworkReply::NoError)
                m_status->setText(QStringLiteral("Model copied to %1, but the original %2 could not be removed: %3").arg(destination, source, deleteErrorText));
            else
                m_status->setText(QStringLiteral("Renamed %1 to %2.").arg(source, destination));
            refreshModels();
        });
    });
}
