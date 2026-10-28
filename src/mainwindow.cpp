#include "mainwindow.h"
#include "modelmanager.h"
#include "updater.h"
#include <QApplication>
#include <QDesktopServices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTimer>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QCompleter>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QImage>
#include <QPixmap>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QStandardPaths>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyleHints>
#include <QTextBrowser>
#include <QTextEdit>
#include <QShortcut>
#include <QTextDocument>
#include <QUuid>
#include <QJsonDocument>
#include <QVBoxLayout>
#include <QSignalBlocker>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincred.h>
#endif
namespace {
struct Palette { QString bg, panel, raised, fg, muted, border, accent, accentText; };
Palette paletteFor(const QString &name) {
    if(name=="ChatGPT Dark") return {"#212121","#171717","#303030","#ececec","#a1a1aa","#414141","#8ab4f8","#171717"};
    if(name=="Light") return {"#f4f6fa","#ffffff","#e9edf5","#182235","#667085","#d7deea","#2563eb","#ffffff"};
    if(name=="Midnight Blue") return {"#080f1f","#0d1930","#142440","#e5efff","#8fa9d0","#263b5e","#60a5fa","#07111f"};
    if(name=="AMOLED Black") return {"#000000","#080808","#171717","#f5f5f5","#a3a3a3","#303030","#38bdf8","#031018"};
    if(name=="Dracula") return {"#282a36","#303241","#44475a","#f8f8f2","#b7b9c9","#50536a","#bd93f9","#21182e"};
    if(name=="Nord") return {"#2e3440","#3b4252","#434c5e","#eceff4","#aab4c4","#4c566a","#88c0d0","#17252a"};
    if(name=="Solarized Dark") return {"#002b36","#073642","#0b414d","#eee8d5","#93a1a1","#24545d","#2aa198","#062b2b"};
    if(name=="Solarized Light") return {"#fdf6e3","#eee8d5","#e5dfcc","#586e75","#839496","#d6cfbb","#268bd2","#ffffff"};
    if(name=="Forest") return {"#0b1712","#11251b","#1b3326","#e4f4e8","#91b5a0","#2c4a38","#4ade80","#082012"};
    if(name=="Sunset") return {"#1b1220","#29182a","#3a2032","#fff0e8","#c7a6b7","#54354d","#fb7185","#2b1018"};
    if(name=="Rose Pine") return {"#191724","#1f1d2e","#26233a","#e0def4","#908caa","#403d52","#ebbcba","#24151a"};
    if(name=="Monokai") return {"#272822","#303127","#414339","#f8f8f2","#b8b8a8","#494a40","#a6e22e","#172006"};
    if(name=="Cyberpunk") return {"#100a20","#1a1030","#291743","#f7efff","#b6a0d4","#49316a","#ff4fd8","#26051f"};
    return {"#0b0e14","#111722","#171f2c","#e7edf7","#8d9bb0","#263246","#58a6ff","#07111f"};
}
QPixmap eclipseLogoPixmap(int size, const QColor &color) {
    // Start transparent so a missing resource can never appear as a solid square.
    QPixmap transparent(size, size);
    transparent.fill(Qt::transparent);

    const QPixmap originalPixmap(QStringLiteral(":/eclipse-logo.png"));
    if (originalPixmap.isNull()) {
        qWarning("Eclipse Infer: failed to load :/eclipse-logo.png from Qt resources");
        return transparent;
    }

    // Use SourceIn to color only the source image's opaque pixels, preserving its
    // alpha mask and antialiased edges instead of replacing every pixel's RGB.
    QPixmap tinted = originalPixmap;
    QPainter painter(&tinted);
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    painter.fillRect(tinted.rect(), color);
    painter.end();

    return tinted.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

QString storedApiKey(const QString &provider) {
#ifdef Q_OS_WIN
    const QString target = QStringLiteral("EclipseInfer/%1").arg(provider);
    PCREDENTIALW credential = nullptr;
    if (CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, &credential)) {
        const QString key = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob), static_cast<qsizetype>(credential->CredentialBlobSize));
        CredFree(credential);
        return key;
    }
    return {};
#else
    return QSettings().value(QStringLiteral("providers/%1/apiKey").arg(provider)).toString();
#endif
}
void storeApiKey(const QString &provider, const QString &key) {
#ifdef Q_OS_WIN
    const QString target = QStringLiteral("EclipseInfer/%1").arg(provider);
    const QByteArray bytes = key.toUtf8();
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(bytes.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(bytes.constData()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"Eclipse Infer");
    CredWriteW(&credential, 0);
#else
    QSettings().setValue(QStringLiteral("providers/%1/apiKey").arg(provider), key);
#endif
}
QString providerEndpoint(const QString &provider) {
    return QSettings().value(QStringLiteral("providers/%1/endpoint").arg(provider), ProviderClient::defaultEndpoint(provider)).toString();
}

QString appStyleSheet(const QString &theme) {
    const Palette p=paletteFor(theme);
    const QString bg=p.bg, panel=p.panel, raised=p.raised, fg=p.fg, muted=p.muted, border=p.border;
    return QString("QWidget{background:%1;color:%2;font-size:13px;} QFrame#Sidebar{background:%3;border-right:1px solid %6;}"
      "QLabel#Brand{font-size:20px;font-weight:700;letter-spacing:0.5px;} QLabel#Eyebrow{color:%4;font-size:11px;font-weight:700;letter-spacing:1px;}"
      "QLabel#Muted{color:%4;font-size:12px;} QPushButton{background:%5;border:1px solid %6;border-radius:9px;padding:9px 12px;}"
      "QPushButton:hover{border-color:%7;} QPushButton#Primary{background:%7;color:%8;border:0;font-weight:700;padding:11px 16px;}"
      "QPushButton#CodeMode{background:transparent;border:1px solid %6;color:%4;padding:8px 11px;} QPushButton#CodeMode:checked{background:%7;color:%8;border-color:%7;font-weight:700;}"
      "QPushButton#Ghost{background:transparent;border-color:transparent;text-align:left;} QPushButton#Ghost:hover{background:%5;border-color:%6;}"
      "QLineEdit,QTextEdit,QComboBox,QSpinBox{background:%3;border:1px solid %6;border-radius:8px;padding:10px;font-size:13px;}"
      "QLineEdit:focus,QComboBox:focus{border-color:%7;} QSlider::groove:horizontal{height:4px;background:%6;}"
      "QSlider::handle:horizontal{background:%7;width:14px;margin:-5px 0;border-radius:7px;}"
      "QScrollArea{border:0;background:transparent;} QScrollBar:vertical{background:transparent;width:8px;}"
      "QScrollBar::handle:vertical{background:%6;min-height:24px;border-radius:4px;} QFrame#Composer{background:%5;border:1px solid %6;border-radius:18px;}"
      "QFrame#MessageCard,QFrame#UserMessage,QFrame#AssistantMessage{background:%3;border:1px solid %6;border-radius:13px;} "
      "QFrame#UserMessage{background:%5;border-color:%6;} QFrame#AssistantMessage{background:%3;} "
      "QTextBrowser{background:transparent;border:0;color:%2;selection-background-color:#244b78;} QTextBrowser a{color:#58a6ff;} QTextBrowser pre{background:%1;border:1px solid %6;border-radius:7px;padding:9px;} QTextBrowser code{font-family:Consolas,monospace;} "
      "QCheckBox{spacing:8px;} QCheckBox::indicator{width:15px;height:15px;border:1px solid %6;border-radius:4px;background:%3;} "
      "QCheckBox::indicator:checked{background:%7;border-color:%7;} "
      "QPushButton:disabled{color:%4;background:%3;border-color:%6;} "
      "QComboBox QAbstractItemView{background:%3;border:1px solid %6;selection-background-color:%7;} "
      "QSpinBox::up-button,QSpinBox::down-button{width:18px;border:0;background:transparent;} "
      "QToolTip{background:%3;color:%2;border:1px solid %6;padding:5px;}").arg(bg,fg,panel,muted,raised,border,p.accent,p.accentText);
}
void setMarkdownContent(QTextBrowser *browser, const QString &markdown) {
    if (!browser) return;
    browser->document()->setDefaultStyleSheet(
        "body { line-height: 1.4; }"
        "h1, h2, h3, h4, h5, h6 { margin-top: 0.8em; margin-bottom: 0.35em; }"
        "p { margin-top: 0.25em; margin-bottom: 0.55em; }"
        "blockquote { margin-left: 0.5em; padding-left: 0.7em; border-left: 3px solid #58a6ff; color: #aab6c8; }"
        "pre { background-color: #101722; border: 1px solid #2b3748; border-radius: 7px; padding: 9px; white-space: pre-wrap; }"
        "code { font-family: Consolas, 'Cascadia Code', monospace; }"
        "table { border-collapse: collapse; }"
        "th, td { border: 1px solid #344155; padding: 5px 8px; }"
        "th { background-color: #202b3a; font-weight: bold; }"
        "a { color: #58a6ff; }"
        "hr { color: #344155; }");
    browser->document()->setMarkdown(markdown, QTextDocument::MarkdownDialectGitHub);
}
QPushButton *button(const QString &text,const QString &name,QWidget *parent) {
    auto *b=new QPushButton(text,parent); b->setObjectName(name); b->setCursor(Qt::PointingHandCursor); return b;
}
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent) {
    resize(1280,820); setMinimumSize(880,600); buildUi();
    connect(&m_providerClient,&ProviderClient::modelsReceivedForProvider,this,[this](const QString &provider,const QStringList &models){
        QSettings settings;
        settings.setValue(QString("providers/%1/cachedModels").arg(provider), models);
        // Every provider's discovery result contributes to the unified model catalog.
        // Build one model catalog across every provider we've discovered so far.
        // Each item carries its real provider and model separately, so choosing a model
        // automatically switches the active provider without opening Settings.
        const QString oldProvider=m_provider->currentText();
        const QString oldModel=m_models->currentData(Qt::UserRole+1).toString();
        const QSignalBlocker modelBlocker(m_models);
        m_models->clear();
        for(const QString &providerName : ProviderClient::providers()) {
            const QStringList providerModels=settings.value(QString("providers/%1/cachedModels").arg(providerName)).toStringList();
            for(const QString &modelName : providerModels) {
                m_models->addItem(QString("%1  ·  %2").arg(modelName,providerName));
                const int row=m_models->count()-1;
                m_models->setItemData(row,providerName,Qt::UserRole);
                m_models->setItemData(row,modelName,Qt::UserRole+1);
            }
        }
        if(m_models->count()==0) m_models->addItem("Discovering models…");
        int selected=-1;
        for(int i=0;i<m_models->count();++i) {
            if(m_models->itemData(i,Qt::UserRole).toString()==oldProvider &&
               m_models->itemData(i,Qt::UserRole+1).toString()==oldModel) { selected=i; break; }
        }
        if(selected<0) {
            for(int i=0;i<m_models->count();++i) {
                if(m_models->itemData(i,Qt::UserRole).toString()==provider) { selected=i; break; }
            }
        }
        if(selected>=0) m_models->setCurrentIndex(selected);
        m_models->setEnabled(m_models->count()>0 && m_models->itemData(0,Qt::UserRole).isValid());
    });
    connect(&m_providerClient,&ProviderClient::connectionChanged,this,[this](bool ok,const QString &msg){
        m_status->setText(QString("●  %1").arg(msg));
        m_status->setStyleSheet(QString("color:%1;font-size:11px;").arg(ok?"#43c78b":"#e58c80"));
    });
    connect(&m_providerClient,&ProviderClient::tokenReceived,this,[this](const QString &part){
        if(m_messages.isEmpty()||m_messages.last().role!="assistant")m_messages.append(Message{"assistant", QString{}, QString{}});
        m_messages.last().content+=part;
        if(m_streamView)setMarkdownContent(m_streamView, m_messages.last().content);
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
    connect(&m_providerClient,&ProviderClient::thinkingReceived,this,[this](const QString &part){
        if(m_messages.isEmpty()||m_messages.last().role!="assistant")m_messages.append(Message{"assistant", QString{}, QString{}});
        m_messages.last().thinking+=part;
        // Persist thinking as it streams so saved chats retain it if generation is interrupted.
        saveCurrentConversation();
        if(m_streamThinkingLabel)m_streamThinkingLabel->setVisible(true);
        if(m_streamThinkingView)setMarkdownContent(m_streamThinkingView, m_messages.last().thinking);
        if(m_streamView && m_messages.last().content.isEmpty())setMarkdownContent(m_streamView, "*Thinking…*");
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
    connect(&m_providerClient,&ProviderClient::errorOccurred,this,[this](const QString &msg){
        m_status->setText(QString("●  %1").arg(msg)); m_status->setStyleSheet("color:#e58c80;font-size:11px;");
    });
    connect(&m_providerClient,&ProviderClient::chatFinished,this,[this](){
        m_busy=false; m_send->setEnabled(true); m_send->setText("Send  ↗"); m_input->setEnabled(true); if(m_stop){m_stop->setVisible(false);m_stop->setEnabled(true);m_stop->setText("Stop");}
        if(m_streamView && !m_messages.isEmpty() && m_messages.last().content.isEmpty())setMarkdownContent(m_streamView, "*No visible response returned.*");
        m_streamView=nullptr;m_streamThinkingView=nullptr;m_streamThinkingLabel=nullptr;
        saveCurrentConversation();
        rebuildHistory();
    });
    QSettings settings;
    const QString initialProvider = settings.value("provider", "Ollama (local)").toString();
    { const QSignalBlocker blocker(m_provider); m_provider->setCurrentText(initialProvider); }
    m_providerClient.configure(initialProvider, providerEndpoint(initialProvider), storedApiKey(initialProvider));
    connect(m_provider, &QComboBox::currentTextChanged, this, [this](const QString &provider) {
        QSettings().setValue("provider", provider);
        m_providerClient.configure(provider, providerEndpoint(provider), storedApiKey(provider));
        refreshModels();
    });
    const QString theme=settings.value("theme","Eclipse Dark").toString();
    applyTheme(theme);
    // Keep the app alive in the Windows notification area when its window closes.
    QApplication::setQuitOnLastWindowClosed(false);
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(windowIcon(), this);
        m_trayIcon->setToolTip(QStringLiteral("Eclipse Infer"));
        auto *trayMenu = new QMenu(this);
        QAction *openAction = trayMenu->addAction(QStringLiteral("Open Eclipse Infer"));
        connect(openAction, &QAction::triggered, this, [this]() {
            showNormal();
            raise();
            activateWindow();
        });
        trayMenu->addSeparator();
        QAction *quitAction = trayMenu->addAction(QStringLiteral("Exit"));
        connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
        m_trayIcon->setContextMenu(trayMenu);
        connect(m_trayIcon, &QSystemTrayIcon::activated, this,
                [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                showNormal();
                raise();
                activateWindow();
            }
        });
        m_trayIcon->show();
    } else {
        QApplication::setQuitOnLastWindowClosed(true);
        qWarning("Eclipse Infer: Windows system tray is not available.");
    }
    m_updateManager = new UpdateManager(this);
    connect(m_updateManager, &UpdateManager::upToDate, this, [this](bool userInitiated) {
        QSettings().setValue("updates/lastCheckUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (userInitiated)
            QMessageBox::information(this, "Eclipse Infer is up to date",
                                     QString("You're running Eclipse Infer %1, the latest stable release.").arg(QApplication::applicationVersion()));
    });
    connect(m_updateManager, &UpdateManager::checkFailed, this, [this](const QString &message, bool userInitiated) {
        if (userInitiated)
            QMessageBox::warning(this, "Update check failed", message);
    });
    connect(m_updateManager, &UpdateManager::updateAvailable, this,
            [this](const QString &version, const QString &notes, const QUrl &assetUrl, const QUrl &releaseUrl) {
        QSettings().setValue("updates/lastCheckUtc", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        const bool canInstall = !assetUrl.isEmpty();
        QMessageBox dialog(this);
        dialog.setWindowTitle("Eclipse Infer update available");
        dialog.setIcon(QMessageBox::Information);
        dialog.setText(QString("Eclipse Infer %1 is available (you have %2).")
                       .arg(version, QApplication::applicationVersion()));
        dialog.setInformativeText(notes.trimmed().isEmpty()
            ? QStringLiteral("View the release notes before updating.")
            : notes.left(1800));
        QPushButton *updateButton = nullptr;
#ifdef Q_OS_WIN
        if (canInstall) updateButton = dialog.addButton("Download and install", QMessageBox::AcceptRole);
#endif
        auto *releaseButton = dialog.addButton("View GitHub release", QMessageBox::ActionRole);
        dialog.addButton("Later", QMessageBox::RejectRole);
        dialog.exec();
        if (dialog.clickedButton() == releaseButton) {
            QDesktopServices::openUrl(releaseUrl);
        }
#ifdef Q_OS_WIN
        else if (updateButton && dialog.clickedButton() == updateButton) {
            m_status->setText(QString("●  Downloading update %1…").arg(version));
            m_updateManager->downloadUpdate(assetUrl, version);
        }
#endif
    });
    connect(m_updateManager, &UpdateManager::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total > 0)
            m_status->setText(QString("●  Downloading update: %1%").arg(qRound(100.0 * received / total)));
        else
            m_status->setText(QString("●  Downloading update… %1 KB").arg(received / 1024));
    });
    connect(m_updateManager, &UpdateManager::downloadFailed, this, [this](const QString &message) {
        QMessageBox::warning(this, "Update download failed", message);
        m_status->setText("●  Update download failed");
    });
    connect(m_updateManager, &UpdateManager::downloadReady, this,
            [this](const QString &path, const QString &version) { installDownloadedUpdate(path, version); });
    auto *helpMenu = menuBar()->addMenu("Help");
    auto *updateAction = helpMenu->addAction("Check for Updates…");
    connect(updateAction, &QAction::triggered, this, [this]() { checkForUpdates(true); });
    const QDateTime lastUpdateCheck = QDateTime::fromString(settings.value("updates/lastCheckUtc").toString(), Qt::ISODate);
    if (!lastUpdateCheck.isValid() || lastUpdateCheck.secsTo(QDateTime::currentDateTimeUtc()) >= 86400) {
        QTimer::singleShot(2500, this, [this]() { checkForUpdates(false); });
    }
    connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,[this](Qt::ColorScheme){
        QSettings s;
        if(s.value("theme","Eclipse Dark").toString()=="System")applyTheme("System");
    });
    m_temperature->setValue(qRound(settings.value("temperature",0.7).toDouble()*10.0));
    m_thinking->setChecked(settings.value("thinking",false).toBool());
    connect(m_temperature,&QSlider::valueChanged,this,[](int v){QSettings().setValue("temperature",v/10.0);});
    connect(m_thinking,&QCheckBox::toggled,this,[](bool v){QSettings().setValue("thinking",v);});
    rebuildHistory(); refreshModels(); showWelcome();
}
void MainWindow::buildUi() {
    auto *root=new QWidget(this); auto *outer=new QHBoxLayout(root); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto *side=new QFrame(root); side->setObjectName("Sidebar"); side->setFixedWidth(286);
    auto *sl=new QVBoxLayout(side); sl->setContentsMargins(17,22,17,18); sl->setSpacing(12);
    auto *brandrow=new QHBoxLayout; m_logoLabel=new QLabel(side); m_logoLabel->setFixedSize(32,32); m_logoLabel->setScaledContents(true);
    auto *brand=new QLabel("ECLIPSE INFER",side); brand->setObjectName("Brand"); brandrow->addWidget(m_logoLabel);brandrow->addWidget(brand,1);sl->addLayout(brandrow);
    auto *tag=new QLabel("LOCAL + HOSTED AI",side);tag->setObjectName("Eyebrow");sl->addWidget(tag);sl->addSpacing(10);
    auto *newButton=button("＋   New chat","Primary",side);sl->addWidget(newButton);connect(newButton,&QPushButton::clicked,this,&MainWindow::newChat);
    auto *modelsButton=button("▦   Model Manager","Ghost",side);sl->addWidget(modelsButton);connect(modelsButton,&QPushButton::clicked,this,&MainWindow::showModelManager);
    auto *recent=new QLabel("RECENT",side);recent->setObjectName("Eyebrow");sl->addWidget(recent);
    auto *historyScroll=new QScrollArea(side);historyScroll->setWidgetResizable(true);historyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *historyHost=new QWidget(historyScroll);m_historyLayout=new QVBoxLayout(historyHost);m_historyLayout->setContentsMargins(0,0,0,0);m_historyLayout->addStretch();historyScroll->setWidget(historyHost);sl->addWidget(historyScroll,1);
    auto *settings=new QLabel("QUICK SETTINGS",side);settings->setObjectName("Eyebrow");sl->addWidget(settings);
    auto *tempRow=new QHBoxLayout;tempRow->addWidget(new QLabel("Temperature",side));tempRow->addStretch();auto *tempVal=new QLabel("0.7",side);tempVal->setObjectName("Muted");tempRow->addWidget(tempVal);sl->addLayout(tempRow);
    m_temperature=new QSlider(Qt::Horizontal,side);m_temperature->setRange(0,20);m_temperature->setValue(7);sl->addWidget(m_temperature);
    connect(m_temperature,&QSlider::valueChanged,this,[tempVal](int v){tempVal->setText(QString::number(v/10.0,'f',1));});
    m_thinking=new QCheckBox("Think",side);m_thinking->setChecked(false);m_thinking->setToolTip("Enable model thinking when the selected provider and model support it.");m_thinking->setCursor(Qt::PointingHandCursor);
    auto *contextRow=new QHBoxLayout;contextRow->addWidget(new QLabel("Context window",side));contextRow->addStretch();m_contextLength=new QSpinBox(side);m_contextLength->setRange(2048,131072);m_contextLength->setSingleStep(2048);m_contextLength->setValue(QSettings().value("contextLength",8192).toInt());m_contextLength->setSuffix(" tokens");m_contextLength->setToolTip("Requested context length. Local runtimes may honor it; hosted providers may ignore it or use their own limits.");m_contextLength->setFixedWidth(125);contextRow->addWidget(m_contextLength);sl->addLayout(contextRow);connect(m_contextLength,qOverload<int>(&QSpinBox::valueChanged),this,[](int v){QSettings().setValue("contextLength",v);});
    auto *bottom=new QHBoxLayout;auto *refresh=button("↻  Refresh","Ghost",side);bottom->addWidget(refresh);sl->addLayout(bottom);
    auto *bottomActions=new QHBoxLayout;bottomActions->setSpacing(6);
    auto *settingsButton=button("⚙  Settings","Ghost",side);bottomActions->addWidget(settingsButton,1);
    bottomActions->addWidget(m_thinking);sl->addLayout(bottomActions);
    connect(settingsButton,&QPushButton::clicked,this,&MainWindow::showSettings);
    connect(refresh,&QPushButton::clicked,this,&MainWindow::refreshModels);outer->addWidget(side);
    auto *main=new QWidget(root);auto *ml=new QVBoxLayout(main);ml->setContentsMargins(28,18,28,20);ml->setSpacing(13);
    auto *top=new QHBoxLayout; m_provider=new QComboBox(main);m_provider->addItems(ProviderClient::providers());m_provider->hide(); auto *mlabel=new QLabel("MODEL",main);mlabel->setObjectName("Eyebrow");top->addWidget(mlabel);
    m_models=new QComboBox(main);m_models->setMinimumWidth(220);m_models->setMaximumWidth(360);m_models->setEditable(true);m_models->setInsertPolicy(QComboBox::NoInsert);m_models->setMaxVisibleItems(18);m_models->setPlaceholderText("Search or choose a model…");m_models->setToolTip("Search and select any discovered model. Eclipse Infer routes requests to its provider automatically.");
    if (m_models->completer()) { m_models->completer()->setCaseSensitivity(Qt::CaseInsensitive); m_models->completer()->setFilterMode(Qt::MatchContains); m_models->completer()->setCompletionMode(QCompleter::PopupCompletion); }
    connect(m_models,qOverload<int>(&QComboBox::currentIndexChanged),this,[this](int index){
        if(index<0 || !m_models->itemData(index,Qt::UserRole).isValid()) return;
        const QString provider=m_models->itemData(index,Qt::UserRole).toString();
        const QString model=m_models->itemData(index,Qt::UserRole+1).toString();
        QSettings().setValue(QString("defaultModel/%1").arg(provider),model);
        if(m_provider->currentText()!=provider) {
            const QSignalBlocker providerBlocker(m_provider);
            m_provider->setCurrentText(provider);
            QSettings().setValue("provider",provider);
            m_providerClient.configure(provider,providerEndpoint(provider),storedApiKey(provider));
            m_status->setText(QString("●  Selected %1").arg(provider));
        }
    });
    top->addWidget(m_models);
    m_status=new QLabel("●  Connecting to Ollama…",main);m_status->setObjectName("Muted");m_status->setWordWrap(true);m_status->setMinimumHeight(22);ml->addLayout(top);ml->addWidget(m_status);
    m_chatTitle=new QLabel("New conversation",main);m_chatTitle->setStyleSheet("font-size:13px;font-weight:600;");ml->addWidget(m_chatTitle);
    m_scroll=new QScrollArea(main);m_scroll->setWidgetResizable(true);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageHost=new QWidget(m_scroll);m_messageLayout=new QVBoxLayout(m_messageHost);m_messageLayout->setContentsMargins(0,8,0,8);m_messageLayout->setSpacing(14);m_messageLayout->addStretch();m_scroll->setWidget(m_messageHost);ml->addWidget(m_scroll,1);
    auto *composer=new QFrame(main);composer->setObjectName("Composer");auto *cl=new QVBoxLayout(composer);cl->setContentsMargins(12,10,12,10);
    m_input=new QTextEdit(composer);m_input->setPlaceholderText("Message your model…  (Ctrl+Enter to send)");m_input->setMinimumHeight(68);m_input->setMaximumHeight(140);m_input->setTabChangesFocus(false);cl->addWidget(m_input);
    auto *cb=new QHBoxLayout;
    m_codeModeButton=button("</>  Code Mode","CodeMode",composer);m_codeModeButton->setCheckable(true);
    m_codeModeButton->setChecked(QSettings().value("codeMode",false).toBool());m_codeMode=m_codeModeButton->isChecked();
    m_input->setPlaceholderText(m_codeMode?"Describe the code you want to build or debug…":"Message your local model…");
    m_codeModeButton->setToolTip("Focus the model on programming, debugging, code review, and runnable examples.");
    connect(m_codeModeButton,&QPushButton::toggled,this,[this](bool enabled){m_codeMode=enabled;QSettings().setValue("codeMode",enabled);m_input->setPlaceholderText(enabled?"Describe the code you want to build or debug…":"Message your local model…");});
    cb->addWidget(m_codeModeButton);cb->addStretch();
    m_stop=button("Stop","",composer);m_stop->setToolTip("Cancel the current generation");m_stop->setVisible(false);cb->addWidget(m_stop);m_send=button("Send  ↗","Primary",composer);cb->addWidget(m_send);cl->addLayout(cb);ml->addWidget(composer);
    connect(m_stop,&QPushButton::clicked,this,[this](){if(m_busy){m_stop->setEnabled(false);m_stop->setText("Stopping…");m_providerClient.cancel();}});
    connect(m_send,&QPushButton::clicked,this,&MainWindow::sendMessage);auto *sendShortcut=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Return),m_input);connect(sendShortcut,&QShortcut::activated,this,&MainWindow::sendMessage);auto *sendShortcut2=new QShortcut(QKeySequence(Qt::CTRL|Qt::Key_Enter),m_input);connect(sendShortcut2,&QShortcut::activated,this,&MainWindow::sendMessage);
    outer->addWidget(main,1);setCentralWidget(root);
}
void MainWindow::applyTheme(const QString &theme){
    const QString resolved=theme=="System"?(QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Light?"Light":"Eclipse Dark"):theme;
    m_dark=resolved!="Light"&&resolved!="Solarized Light";
    const Palette p=paletteFor(resolved);
    const QIcon logoIcon(QPixmap::fromImage(eclipseLogoPixmap(128,QColor(p.accent)).toImage()));
    if(m_logoLabel)m_logoLabel->setPixmap(eclipseLogoPixmap(64,QColor(p.accent)));
    if(m_welcomeLogo)m_welcomeLogo->setPixmap(eclipseLogoPixmap(96,QColor(p.accent)));
    setWindowIcon(logoIcon);
    qApp->setWindowIcon(logoIcon);
    qApp->setStyleSheet(appStyleSheet(resolved));
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_trayIcon && m_trayIcon->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    event->accept();
}
void MainWindow::refreshModels(){
    const QString activeProvider=m_provider?m_provider->currentText():QSettings().value("provider","Ollama (local)").toString();
    m_status->setText("●  Discovering available models…");
    // Discover local runtimes automatically. Query hosted providers only when an API key
    // is configured, avoiding needless authentication failures on a fresh install.
    for (const QString &provider : ProviderClient::providers()) {
        const bool local = provider == "Ollama (local)" || provider == "LM Studio (local)" ||
            provider == "llama.cpp (local)" || provider == "vLLM (local)" || provider == "LocalAI";
        if (!local && storedApiKey(provider).isEmpty()) continue;
        m_providerClient.configure(provider, providerEndpoint(provider), storedApiKey(provider));
        m_providerClient.refreshModels();
    }
    m_providerClient.configure(activeProvider, providerEndpoint(activeProvider), storedApiKey(activeProvider));
}
void MainWindow::showModelManager(){ ModelManagerDialog dialog(this); dialog.exec(); refreshModels(); }
void MainWindow::checkForUpdates(bool userInitiated) {
    if (userInitiated) m_status->setText("●  Checking GitHub for updates…");
    m_updateManager->checkForUpdates(userInitiated);
}
void MainWindow::installDownloadedUpdate(const QString &path, const QString &version) {
#ifdef Q_OS_WIN
    const QString executable = QCoreApplication::applicationFilePath();
    const QFileInfo executableInfo(executable);
    if (!QFileInfo(executableInfo.dir().absolutePath()).isWritable()) {
        QMessageBox::information(this, "Update downloaded",
            "The update was downloaded, but Eclipse Infer is installed in a protected folder. Open the GitHub release page and run the update with the required permissions.");
        QDesktopServices::openUrl(QUrl("https://github.com/LinkZelda99-cpp/Eclipse-Infer/releases/latest"));
        return;
    }
    const QString scriptPath = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath("EclipseInfer-install-update.ps1");
    auto psQuote = [](QString value) {
        value.replace("'", "''");
        return QString("'") + value + QString("'");
    };
    const QString source = QDir::toNativeSeparators(path);
    const QString targetDir = QDir::toNativeSeparators(executableInfo.dir().absolutePath());
    const QString staging = QString("EclipseInfer-update-%1").arg(version);
    const QString body = QStringLiteral(
        "$ErrorActionPreference = 'Stop'\r\n"
        "Start-Sleep -Seconds 3\r\n"
        "$archive = %1\r\n"
        "$targetDir = %2\r\n"
        "$tempDir = Join-Path $env:TEMP %3\r\n"
        "try {\r\n"
        "  if (Test-Path $tempDir) { Remove-Item $tempDir -Recurse -Force }\r\n"
        "  if ($archive.EndsWith('.zip', [System.StringComparison]::OrdinalIgnoreCase)) {\r\n"
        "    Expand-Archive -LiteralPath $archive -DestinationPath $tempDir -Force\r\n"
        "    Copy-Item -Path (Join-Path $tempDir '*') -Destination $targetDir -Recurse -Force\r\n"
        "  } else { Copy-Item -LiteralPath $archive -Destination (Join-Path $targetDir 'EclipseInfer.exe') -Force }\r\n"
        "  Start-Process (Join-Path $targetDir 'EclipseInfer.exe')\r\n"
        "  Remove-Item $archive -Force -ErrorAction SilentlyContinue\r\n"
        "  Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue\r\n"
        "} catch {\r\n"
        "  Start-Process 'https://github.com/LinkZelda99-cpp/Eclipse-Infer/releases/latest'\r\n"
        "  Write-Host ('Update failed: ' + $_.Exception.Message)\r\n"
        "  Read-Host 'Press Enter to close'\r\n"
        "} finally { Remove-Item -LiteralPath $MyInvocation.MyCommand.Path -Force -ErrorAction SilentlyContinue }\r\n")
        .arg(psQuote(source), psQuote(targetDir), psQuote(staging));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, "Update error", "Couldn't prepare the updater script.");
        return;
    }
    script.write(body.toUtf8());
    script.close();
    if (!QProcess::startDetached("powershell.exe",
        {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", scriptPath})) {
        QMessageBox::warning(this, "Update error", "Couldn't start the installer. You can download the release manually from GitHub.");
        QDesktopServices::openUrl(QUrl("https://github.com/LinkZelda99-cpp/Eclipse-Infer/releases/latest"));
        return;
    }
    QMessageBox::information(this, "Installing update",
        QString("Eclipse Infer %1 has been downloaded. The app will close, update its files, and restart.").arg(version));
    qApp->quit();
#else
    Q_UNUSED(path);
    QMessageBox::information(this, "Update downloaded",
        QString("Eclipse Infer %1 is available. Open the release page to install it for your platform.").arg(version));
    QDesktopServices::openUrl(QUrl("https://github.com/LinkZelda99-cpp/Eclipse-Infer/releases/latest"));
#endif
}
void MainWindow::addMessageCard(const QString &role,const QString &content,bool streaming,const QString &thinking) {
    auto *card=new QFrame(m_messageHost);card->setObjectName(role=="user"?"UserMessage":"AssistantMessage");
    card->setProperty("messageRole",role);
    auto *layout=new QVBoxLayout(card);layout->setContentsMargins(18,14,18,14);layout->setSpacing(9);

    // Keep the role and message actions together so long replies remain easy to scan.
    auto *header=new QHBoxLayout;header->setSpacing(8);
    auto *label=new QLabel((role=="user"?"YOU":"ECLIPSE INFER"),card);
    label->setStyleSheet(QString("font-size:10px;font-weight:800;letter-spacing:1px;color:%1;").arg(role=="user"?"#58a6ff":"#8d9bb0"));
    header->addWidget(label);header->addStretch();
    auto *copyButton=button("Copy","MessageAction",card);
    copyButton->setToolTip("Copy this message as text");
    copyButton->setStyleSheet("QPushButton{background:transparent;border:1px solid transparent;border-radius:6px;padding:3px 7px;color:#8d9bb0;font-size:11px;} QPushButton:hover{background:rgba(128,128,128,0.12);border-color:rgba(128,128,128,0.25);}");
    header->addWidget(copyButton);layout->addLayout(header);

    QPointer<QTextBrowser> thinkingView;
    if(role=="assistant"){
        auto *thinkingToggle=button("▸  Model thinking","Ghost",card);
        thinkingToggle->setStyleSheet("QPushButton{background:transparent;border:0;padding:3px 0;text-align:left;color:#58a6ff;font-size:11px;font-weight:700;} QPushButton:hover{color:#8bc4ff;}");
        layout->addWidget(thinkingToggle);
        auto *tv=new QTextBrowser(card);tv->setReadOnly(true);tv->setOpenExternalLinks(true);setMarkdownContent(tv, thinking);tv->setMinimumHeight(24);tv->setMaximumHeight(180);tv->setVisible(false);tv->setFrameShape(QFrame::NoFrame);
        tv->setStyleSheet("QTextBrowser{background:transparent;border:0;padding:0;color:#8d9bb0;}");layout->addWidget(tv);thinkingView=tv;
        connect(thinkingToggle,&QPushButton::clicked,card,[thinkingToggle,tv](){
            const bool expand=!tv->isVisible();
            tv->setVisible(expand);
            thinkingToggle->setText(QString("%1  Model thinking").arg(expand?"▾":"▸"));
        });
        if(streaming){m_streamThinkingLabel=thinkingToggle;}
    }
    auto *view=new QTextBrowser(card);view->setReadOnly(true);view->setOpenExternalLinks(true);setMarkdownContent(view, content);view->setMinimumHeight(28);view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet("QTextBrowser{background:transparent;border:0;padding:0;}");layout->addWidget(view);
    connect(copyButton,&QPushButton::clicked,card,[copyButton,view](){
        QApplication::clipboard()->setText(view->toPlainText());
        copyButton->setText("Copied");
        QTimer::singleShot(1200,copyButton,[copyButton](){if(copyButton)copyButton->setText("Copy");});
    });
    m_messageLayout->insertWidget(m_messageLayout->count()-1,card);
    if(streaming){m_streamView=view;m_streamThinkingView=thinkingView;}
}
void MainWindow::sendMessage() {
    if(m_busy)return;const QString prompt=m_input->toPlainText().trimmed();if(prompt.isEmpty())return;
    if(m_models->currentText().isEmpty()||!m_models->currentData(Qt::UserRole).isValid()){QMessageBox::information(this,"No model selected","Choose a model from the selector. Open Model Manager to add a local model, or Settings to configure a hosted provider.");return;}
    if(m_messages.isEmpty()) {
        // Remove the welcome panel as soon as the first message is sent.
        if (m_messageLayout->count() > 1) {
            auto *welcomeItem = m_messageLayout->takeAt(0);
            if (welcomeItem->widget()) welcomeItem->widget()->deleteLater();
            delete welcomeItem;
        }
    }
    if(m_title.isEmpty()){m_title=prompt.simplified();if(m_title.size()>34)m_title=m_title.left(31)+"…";m_chatTitle->setText(m_title);}
    if(m_conversationId.isEmpty())m_conversationId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_messages.append(Message{"user", prompt, QString{}});addMessageCard("user",prompt);m_input->clear();
    saveCurrentConversation();
    addMessageCard("assistant","*Generating…*",true);m_busy=true;m_send->setEnabled(false);m_send->setText("Generating…");m_input->setEnabled(false);if(m_stop){m_stop->setEnabled(true);m_stop->setText("Stop");m_stop->setVisible(true);}
    const QString selectedProvider=m_models->currentData(Qt::UserRole).toString();
    const QString selectedModel=m_models->currentData(Qt::UserRole+1).toString();
    m_providerClient.configure(selectedProvider,providerEndpoint(selectedProvider),storedApiKey(selectedProvider));
    m_providerClient.chat(selectedModel,apiMessages(),m_thinking->isChecked(),m_temperature->value()/10.0,m_contextLength ? m_contextLength->value() : QSettings().value("contextLength",8192).toInt());
}
void MainWindow::newChat() {
    if(m_busy)m_providerClient.cancel();
    saveCurrentConversation();
    m_messages.clear();m_title.clear();m_conversationId.clear();if(m_stop){m_stop->setVisible(false);m_stop->setEnabled(true);m_stop->setText("Stop");}m_streamView=nullptr;m_streamThinkingView=nullptr;m_streamThinkingLabel=nullptr;m_busy=false;m_send->setEnabled(true);m_send->setText("Send  ↗");m_input->setEnabled(true);m_chatTitle->setText("New conversation");
    while(m_messageLayout->count()>1){auto *item=m_messageLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}showWelcome();rebuildHistory();
}
void MainWindow::showWelcome() {
    if(!m_messages.isEmpty()||m_messageLayout->count()>1)return;
    auto *w=new QWidget(m_messageHost);
    auto *l=new QVBoxLayout(w);
    l->setContentsMargins(20,48,20,48);
    l->setSpacing(18);
    l->setAlignment(Qt::AlignHCenter|Qt::AlignVCenter);

    m_welcomeLogo=new QLabel(w);
    m_welcomeLogo->setAlignment(Qt::AlignCenter);
    m_welcomeLogo->setFixedSize(112,112);
    m_welcomeLogo->setScaledContents(true);
    m_welcomeLogo->setPixmap(eclipseLogoPixmap(112,QColor(paletteFor(QSettings().value("theme","Eclipse Dark").toString()=="System"?(QGuiApplication::styleHints()->colorScheme()==Qt::ColorScheme::Light?"Light":"Eclipse Dark"):QSettings().value("theme","Eclipse Dark").toString()).accent)));
    l->addWidget(m_welcomeLogo,0,Qt::AlignHCenter);

    auto *title=new QLabel("Your AI. Your workspace.",w);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size:34px;font-weight:700;letter-spacing:-0.5px;");
    l->addWidget(title);
    m_messageLayout->insertWidget(0,w);
}
QJsonArray MainWindow::apiMessages() const {
    QJsonArray a;
    if(m_codeMode){
        QJsonObject system;
        system.insert("role","system");
        system.insert("content",QStringLiteral(
            "You are in Code Mode, a focused software engineering workspace. Help the user build, understand, debug, refactor, and review software. "
            "Prefer correct, maintainable, idiomatic code and respect the language, framework, versions, and conventions the user specifies. "
            "When changing existing code, preserve unrelated behavior and explain important changes. For debugging, identify the likely root cause, state assumptions, and give a concrete fix. "
            "Use fenced code blocks with an accurate language tag for code. Keep explanations concise but useful; put the solution first when the request is clear. "
            "Provide complete, runnable examples when appropriate, but do not invent APIs, dependencies, test results, or facts about files you have not seen. "
            "If essential requirements are missing, ask a focused clarifying question instead of guessing. Mention relevant edge cases and security concerns when they matter."));
        a.append(system);
    }
    for(const auto &m:m_messages){QJsonObject o;o.insert("role",m.role);o.insert("content",m.content);a.append(o);}
    return a;
}
void MainWindow::saveCurrentConversation() {
    if(m_conversationId.isEmpty()||m_messages.isEmpty())return;
    QJsonArray messages;
    for(const auto &message:m_messages){QJsonObject item;item.insert("role",message.role);item.insert("content",message.content);if(!message.thinking.isEmpty())item.insert("thinking",message.thinking);messages.append(item);}
    QSettings settings;
    settings.setValue("conversations/"+m_conversationId+"/title",m_title.isEmpty()?"New conversation":m_title);
    settings.setValue("conversations/"+m_conversationId+"/messages",QString::fromUtf8(QJsonDocument(messages).toJson(QJsonDocument::Compact)));
    settings.setValue("conversations/"+m_conversationId+"/model",m_models->currentData(Qt::UserRole+1).toString().isEmpty()?m_models->currentText():m_models->currentData(Qt::UserRole+1).toString());
    settings.setValue("conversations/"+m_conversationId+"/provider",m_models->currentData(Qt::UserRole).toString().isEmpty()?m_provider->currentText():m_models->currentData(Qt::UserRole).toString());
    m_historyIds.removeAll(m_conversationId);m_historyIds.prepend(m_conversationId);
    while(m_historyIds.size()>settings.value("historyLimit",30).toInt()){const QString old=m_historyIds.takeLast();settings.remove("conversations/"+old);}
    settings.setValue("conversationIds",m_historyIds);
}
void MainWindow::rebuildHistory() {
    while(m_historyLayout->count()>1){auto *item=m_historyLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}
    QSettings settings;
    m_historyIds=settings.value("conversationIds").toStringList();
    for(const QString &id:m_historyIds){
        const QString title=settings.value("conversations/"+id+"/title","Untitled chat").toString();
        auto *b=button("◦  "+title,"Ghost",this);b->setToolTip(title);m_historyLayout->insertWidget(m_historyLayout->count()-1,b);
        connect(b,&QPushButton::clicked,this,[this,id](){openConversation(id);});
    }
}
void MainWindow::openConversation(const QString &id) {
    if(m_busy){QMessageBox::information(this,"Response in progress","Stop or wait for the current response before switching chats.");return;}
    saveCurrentConversation();
    QSettings settings;const QString raw=settings.value("conversations/"+id+"/messages").toString();
    const auto document=QJsonDocument::fromJson(raw.toUtf8());if(!document.isArray())return;
    m_messages.clear();m_conversationId=id;m_title=settings.value("conversations/"+id+"/title").toString();
    const QString model=settings.value("conversations/"+id+"/model").toString();const QString provider=settings.value("conversations/"+id+"/provider",m_provider->currentText()).toString();int modelIndex=-1;for(int i=0;i<m_models->count();++i)if(m_models->itemData(i,Qt::UserRole).toString()==provider&&m_models->itemData(i,Qt::UserRole+1).toString()==model){modelIndex=i;break;}if(modelIndex>=0)m_models->setCurrentIndex(modelIndex);
    while(m_messageLayout->count()>1){auto *item=m_messageLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}
    for(const auto &value:document.array()){
        const auto object=value.toObject();const QString role=object.value("role").toString();const QString content=object.value("content").toString();const QString thinking=object.value("thinking").toString();
        if(role!="user"&&role!="assistant")continue;
        m_messages.append(Message{role,content,thinking});addMessageCard(role,content,false,thinking);
    }
    m_streamView=nullptr;m_streamThinkingView=nullptr;m_streamThinkingLabel=nullptr;
    m_chatTitle->setText(m_title);m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}
void MainWindow::showSettings() {
    QDialog dialog(this);dialog.setWindowTitle("Eclipse Infer Settings");dialog.setMinimumWidth(420);
    auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;
    auto *theme=new QComboBox(&dialog);
    const QStringList themes={"System","ChatGPT Dark","Eclipse Dark","Light","Midnight Blue","AMOLED Black","Dracula","Nord","Solarized Dark","Solarized Light","Forest","Sunset","Rose Pine","Monokai","Cyberpunk"};
    theme->addItems(themes);
    QSettings settings;int themeIndex=theme->findText(settings.value("theme","Eclipse Dark").toString());if(themeIndex>=0)theme->setCurrentIndex(themeIndex);
    connect(theme,&QComboBox::currentTextChanged,this,[this](const QString &name){applyTheme(name);});
    auto *provider = new QComboBox(&dialog); provider->addItems(ProviderClient::providers()); provider->setCurrentText(m_provider->currentText());
    auto *endpoint = new QLineEdit(providerEndpoint(provider->currentText()), &dialog);
    auto *apiKey = new QLineEdit(storedApiKey(provider->currentText()), &dialog); apiKey->setEchoMode(QLineEdit::Password); apiKey->setPlaceholderText("Required for most hosted providers");
    connect(provider, &QComboBox::currentTextChanged, &dialog, [endpoint, apiKey](const QString &name) { endpoint->setText(providerEndpoint(name)); apiKey->setText(storedApiKey(name)); });
    auto *contextLength=new QSpinBox(&dialog);contextLength->setRange(2048,131072);contextLength->setSingleStep(2048);contextLength->setValue(settings.value("contextLength",8192).toInt());
    auto *historyLimit=new QComboBox(&dialog);historyLimit->addItem("10 conversations",10);historyLimit->addItem("20 conversations",20);historyLimit->addItem("30 conversations",30);
    int limitIndex=historyLimit->findData(settings.value("historyLimit",30));if(limitIndex>=0)historyLimit->setCurrentIndex(limitIndex);
    auto *clearHistory=button("Delete all saved conversations","", &dialog);
    form->addRow("Appearance",theme);form->addRow("Provider",provider);form->addRow("API endpoint",endpoint);form->addRow("API key",apiKey);form->addRow("Context window (tokens)",contextLength);form->addRow("Keep recent chats",historyLimit);layout->addLayout(form);
    auto *privacy=new QLabel("Chats and preferences are stored locally. Prompts are sent to the selected provider; cloud providers receive prompts over the internet.",&dialog);privacy->setWordWrap(true);privacy->setObjectName("Muted");layout->addWidget(privacy);layout->addWidget(clearHistory);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    connect(clearHistory,&QPushButton::clicked,&dialog,[this,&dialog](){if(QMessageBox::question(&dialog,"Delete saved chats","Permanently delete all saved conversation history?")==QMessageBox::Yes){QSettings s;const auto ids=s.value("conversationIds").toStringList();for(const auto &id:ids)s.remove("conversations/"+id);s.remove("conversationIds");m_historyIds.clear();rebuildHistory();}});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,[this,&dialog,&settings](){applyTheme(settings.value("theme","Eclipse Dark").toString());dialog.reject();});
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[this,&dialog,theme,provider,endpoint,apiKey,contextLength,historyLimit](){
        QSettings s;
        s.setValue("provider",provider->currentText());
        s.setValue(QStringLiteral("providers/%1/endpoint").arg(provider->currentText()),endpoint->text().trimmed());
        storeApiKey(provider->currentText(),apiKey->text().trimmed());
        { const QSignalBlocker blocker(m_provider); m_provider->setCurrentText(provider->currentText()); }
        m_providerClient.configure(provider->currentText(),endpoint->text().trimmed(),apiKey->text().trimmed());
        s.setValue("theme",theme->currentText());s.setValue("contextLength",contextLength->value());if(m_contextLength)m_contextLength->setValue(contextLength->value());s.setValue("historyLimit",historyLimit->currentData().toInt());
        applyTheme(theme->currentText());
        dialog.accept();
        refreshModels();
    });
    dialog.exec();
}
