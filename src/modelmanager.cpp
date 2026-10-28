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
#include <QComboBox>
#include <QGroupBox>
#include <QDesktopServices>
#include <QSettings>
#include <QUrl>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif

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
QString readProviderKey(const QString &provider) {
#ifdef Q_OS_WIN
    const QString target = QStringLiteral("EclipseInfer/%1").arg(provider);
    PCREDENTIALW credential = nullptr;
    if (CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        const QString key = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
                                               static_cast<qsizetype>(credential->CredentialBlobSize));
        CredFree(credential);
        return key;
    }
    return {};
#else
    return QSettings().value(QStringLiteral("providers/%1/apiKey").arg(provider)).toString();
#endif
}
void writeProviderKey(const QString &provider, const QString &key) {
#ifdef Q_OS_WIN
    const QString target = QStringLiteral("EclipseInfer/%1").arg(provider);
    if (key.isEmpty()) {
        CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0);
        return;
    }
    const QByteArray bytes = key.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"Eclipse Orbit");
    CredWriteW(&credential, 0);
#else
    QSettings settings;
    const QString path = QStringLiteral("providers/%1/apiKey").arg(provider);
    if (key.isEmpty()) settings.remove(path);
    else settings.setValue(path, key);
#endif
}
QString keyHelpUrl(const QString &provider) {
    if (provider == QStringLiteral("OpenAI")) return QStringLiteral("https://platform.openai.com/api-keys");
    if (provider == QStringLiteral("OpenRouter")) return QStringLiteral("https://openrouter.ai/keys");
    if (provider == QStringLiteral("Groq")) return QStringLiteral("https://console.groq.com/keys");
    if (provider == QStringLiteral("Together AI")) return QStringLiteral("https://api.together.ai/settings/api-keys");
    if (provider == QStringLiteral("DeepSeek")) return QStringLiteral("https://platform.deepseek.com/api_keys");
    if (provider == QStringLiteral("Mistral")) return QStringLiteral("https://console.mistral.ai/api-keys/");
    if (provider == QStringLiteral("Anthropic")) return QStringLiteral("https://console.anthropic.com/settings/keys");
    if (provider == QStringLiteral("Google Gemini")) return QStringLiteral("https://aistudio.google.com/app/apikey");
    return {};
}
}

ModelManagerDialog::ModelManagerDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Model Manager — Eclipse Orbit"));
    resize(900, 620);
    setMinimumSize(720, 500);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 20, 22, 18);
    root->setSpacing(12);

    auto *heading = new QLabel(QStringLiteral("Model Manager"), this);
    heading->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
    root->addWidget(heading);
    auto *description = new QLabel(
        QStringLiteral("Manage local model downloads and removals, or configure cloud providers and API keys here. Local model weights stay on your device; cloud providers process prompts on their services."),
        this);
    description->setWordWrap(true);
    root->addWidget(description);

    // Cloud provider configuration lives in the same manager as local model downloads.
    auto *cloudGroup = new QGroupBox(QStringLiteral("Cloud / API models"), this);
    auto *cloudLayout = new QFormLayout(cloudGroup);
    auto *cloudProvider = new QComboBox(cloudGroup);
    cloudProvider->addItems({QStringLiteral("OpenAI"), QStringLiteral("OpenRouter"), QStringLiteral("Groq"),
        QStringLiteral("Together AI"), QStringLiteral("DeepSeek"), QStringLiteral("Mistral"),
        QStringLiteral("Anthropic"), QStringLiteral("Google Gemini"), QStringLiteral("Custom OpenAI-compatible")});
    auto *cloudEndpoint = new QLineEdit(cloudGroup);
    auto *cloudKey = new QLineEdit(cloudGroup);
    cloudKey->setEchoMode(QLineEdit::Password);
    cloudKey->setPlaceholderText(QStringLiteral("Paste API key (stored in Windows Credential Manager)"));
    auto *cloudHelp = new QLabel(cloudGroup);
    cloudHelp->setWordWrap(true);
    auto *cloudButtons = new QHBoxLayout;
    auto *openKeyHelp = new QPushButton(QStringLiteral("Get API key / setup guide"), cloudGroup);
    auto *saveCloudKey = new QPushButton(QStringLiteral("Save provider"), cloudGroup);
    auto *deleteCloudKey = new QPushButton(QStringLiteral("Delete saved key"), cloudGroup);
    cloudButtons->addWidget(openKeyHelp);
    cloudButtons->addStretch();
    cloudButtons->addWidget(deleteCloudKey);
    cloudButtons->addWidget(saveCloudKey);
    cloudLayout->addRow(QStringLiteral("Provider"), cloudProvider);
    cloudLayout->addRow(QStringLiteral("API endpoint"), cloudEndpoint);
    cloudLayout->addRow(QStringLiteral("API key"), cloudKey);
    cloudLayout->addRow(QStringLiteral("Help"), cloudHelp);
    cloudLayout->addRow(QString{}, cloudButtons);
    root->addWidget(cloudGroup);

    auto *configuredCloudModels = new QTreeWidget(this);
    configuredCloudModels->setColumnCount(3);
    configuredCloudModels->setHeaderLabels({QStringLiteral("Cloud provider"), QStringLiteral("API key"), QStringLiteral("Available models")});
    configuredCloudModels->setRootIsDecorated(false);
    configuredCloudModels->setMaximumHeight(170);
    configuredCloudModels->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    configuredCloudModels->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    configuredCloudModels->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    root->addWidget(new QLabel(QStringLiteral("Configured cloud providers"), this));
    root->addWidget(configuredCloudModels);

    auto refreshConfiguredCloudModels = [configuredCloudModels]() {
        configuredCloudModels->clear();
        const QStringList providers = {QStringLiteral("OpenAI"), QStringLiteral("OpenRouter"), QStringLiteral("Groq"),
            QStringLiteral("Together AI"), QStringLiteral("DeepSeek"), QStringLiteral("Mistral"),
            QStringLiteral("Anthropic"), QStringLiteral("Google Gemini"), QStringLiteral("Custom OpenAI-compatible")};
        QSettings settings;
        for (const QString &provider : providers) {
            const QString key = readProviderKey(provider);
            const QStringList models = settings.value(QStringLiteral("providers/%1/cachedModels").arg(provider)).toStringList();
            const QString endpoint = settings.value(QStringLiteral("providers/%1/endpoint").arg(provider)).toString();
            if (key.isEmpty() && endpoint.isEmpty() && models.isEmpty()) continue;
            auto *item = new QTreeWidgetItem(configuredCloudModels);
            item->setText(0, provider);
            item->setText(1, key.isEmpty() ? QStringLiteral("Not set") : QStringLiteral("Saved"));
            item->setText(2, models.isEmpty() ? QStringLiteral("Refresh models from the home page") : models.join(QStringLiteral(", ")));
            item->setToolTip(2, models.join(QStringLiteral("\\n")));
        }
        if (configuredCloudModels->topLevelItemCount() == 0) {
            auto *item = new QTreeWidgetItem(configuredCloudModels);
            item->setText(0, QStringLiteral("No cloud providers configured yet"));
            item->setText(2, QStringLiteral("Choose a provider above and save its settings."));
        }
    };

    auto refreshCloudFields = [cloudProvider, cloudEndpoint, cloudKey, cloudHelp, openKeyHelp]() {
        const QString provider = cloudProvider->currentText();
        const QString defaultEndpoint =
            provider == QStringLiteral("Custom OpenAI-compatible") ? QStringLiteral("https://api.openai.com/v1") :
            provider == QStringLiteral("OpenAI") ? QStringLiteral("https://api.openai.com/v1") :
            provider == QStringLiteral("OpenRouter") ? QStringLiteral("https://openrouter.ai/api/v1") :
            provider == QStringLiteral("Groq") ? QStringLiteral("https://api.groq.com/openai/v1") :
            provider == QStringLiteral("Together AI") ? QStringLiteral("https://api.together.xyz/v1") :
            provider == QStringLiteral("DeepSeek") ? QStringLiteral("https://api.deepseek.com/v1") :
            provider == QStringLiteral("Mistral") ? QStringLiteral("https://api.mistral.ai/v1") :
            provider == QStringLiteral("Anthropic") ? QStringLiteral("https://api.anthropic.com/v1") :
            QStringLiteral("https://generativelanguage.googleapis.com/v1beta");
        cloudEndpoint->setText(QSettings().value(QStringLiteral("providers/%1/endpoint").arg(provider), defaultEndpoint).toString());
        cloudKey->setText(readProviderKey(provider));
        const QString help = keyHelpUrl(provider);
        openKeyHelp->setEnabled(!help.isEmpty());
        cloudHelp->setText(help.isEmpty()
            ? QStringLiteral("Enter your provider's OpenAI-compatible base URL. Create a key in that provider's developer dashboard.")
            : QStringLiteral("Open the provider's official key page. Follow its instructions to create a key, then paste it here."));
    };
    connect(cloudProvider, &QComboBox::currentTextChanged, this, [refreshCloudFields](const QString &) { refreshCloudFields(); });
    connect(openKeyHelp, &QPushButton::clicked, this, [cloudProvider]() {
        const QString help = keyHelpUrl(cloudProvider->currentText());
        if (!help.isEmpty()) QDesktopServices::openUrl(QUrl(help));
    });
    connect(saveCloudKey, &QPushButton::clicked, this, [cloudProvider, cloudEndpoint, cloudKey, refreshConfiguredCloudModels, this]() {
        const QString provider = cloudProvider->currentText();
        const QString endpoint = cloudEndpoint->text().trimmed();
        if (endpoint.isEmpty() || !QUrl(endpoint).isValid() ||
            (QUrl(endpoint).scheme() != QStringLiteral("http") && QUrl(endpoint).scheme() != QStringLiteral("https"))) {
            QMessageBox::warning(this, QStringLiteral("Invalid API endpoint"), QStringLiteral("Enter a valid http:// or https:// base URL."));
            return;
        }
        QSettings settings;
        settings.setValue(QStringLiteral("providers/%1/endpoint").arg(provider), endpoint);
        writeProviderKey(provider, cloudKey->text().trimmed());
        settings.setValue(QStringLiteral("provider"), provider);
        refreshConfiguredCloudModels();
        QMessageBox::information(this, QStringLiteral("Provider saved"),
            QStringLiteral("%1 settings saved. Select this provider in the main model selector to load its available models.").arg(provider));
    });
    connect(deleteCloudKey, &QPushButton::clicked, this, [cloudProvider, cloudKey, refreshConfiguredCloudModels, this]() {
        const QString provider = cloudProvider->currentText();
        if (QMessageBox::question(this, QStringLiteral("Delete API key"),
            QStringLiteral("Remove the saved API key for %1 from this device?").arg(provider),
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        writeProviderKey(provider, {});
        cloudKey->clear();
        refreshConfiguredCloudModels();
    });
    refreshCloudFields();
    refreshConfiguredCloudModels();

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
    m_modelName->setPlaceholderText(QStringLiteral("Or type any Ollama model, e.g. qwen2.5:7b"));
    m_modelName->setClearButtonEnabled(true);
    m_popularModels = new QComboBox(this);
    m_popularModels->addItems({QStringLiteral("Choose a popular model…"), QStringLiteral("qwen2.5:7b"),
        QStringLiteral("llama3.2"), QStringLiteral("gemma3:4b"), QStringLiteral("deepseek-r1:7b"),
        QStringLiteral("mistral:7b"), QStringLiteral("phi4:14b")});
    connect(m_popularModels, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (m_popularModels->currentIndex() > 0) m_modelName->setText(name);
    });
    auto *addModelRow = new QHBoxLayout;
    addModelRow->addWidget(m_popularModels);
    addModelRow->addWidget(m_modelName, 1);
    downloadGroup->addRow(QStringLiteral("Add a local model"), addModelRow);
    root->addLayout(downloadGroup);

    auto *actions = new QHBoxLayout;
    m_pullButton = new QPushButton(QStringLiteral("Add / Download model"), this);
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
        QStringLiteral("Ollama does not provide an in-place rename. Eclipse Orbit will copy “%1” to “%2”, then remove the original only if the copy succeeds.").arg(source, destination),
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
