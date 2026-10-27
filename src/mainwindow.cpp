#include "mainwindow.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QComboBox>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyleHints>
#include <QTextBrowser>
#include <QUuid>
#include <QJsonDocument>
#include <QVBoxLayout>
namespace {
QString appStyleSheet(bool dark) {
    const QString bg=dark?"#0b0e14":"#f4f6fa", panel=dark?"#111722":"#ffffff";
    const QString raised=dark?"#171f2c":"#e9edf5", fg=dark?"#e7edf7":"#182235";
    const QString muted=dark?"#8d9bb0":"#667085", border=dark?"#263246":"#d7deea";
    return QString("QWidget{background:%1;color:%2;} QFrame#Sidebar{background:%3;border-right:1px solid %6;}"
      "QLabel#Brand{font-size:19px;font-weight:700;letter-spacing:1px;} QLabel#Eyebrow{color:%4;font-size:10px;font-weight:700;letter-spacing:1.5px;}"
      "QLabel#Muted{color:%4;font-size:11px;} QPushButton{background:%5;border:1px solid %6;border-radius:9px;padding:9px 12px;}"
      "QPushButton:hover{border-color:#58a6ff;} QPushButton#Primary{background:#58a6ff;color:#07111f;border:0;font-weight:700;padding:11px 16px;}"
      "QPushButton#Ghost{background:transparent;border-color:transparent;text-align:left;} QPushButton#Ghost:hover{background:%5;border-color:%6;}"
      "QLineEdit,QComboBox,QSpinBox{background:%3;border:1px solid %6;border-radius:8px;padding:9px;}"
      "QLineEdit:focus,QComboBox:focus{border-color:#58a6ff;} QSlider::groove:horizontal{height:4px;background:%6;}"
      "QSlider::handle:horizontal{background:#58a6ff;width:14px;margin:-5px 0;border-radius:7px;}"
      "QScrollArea{border:0;background:transparent;} QScrollBar:vertical{background:transparent;width:8px;}"
      "QScrollBar::handle:vertical{background:%6;min-height:24px;border-radius:4px;} QFrame#Composer{background:%3;border:1px solid %6;border-radius:14px;}"
      "QFrame#MessageCard,QFrame#UserMessage,QFrame#AssistantMessage{background:%3;border:1px solid %6;border-radius:13px;} "
      "QFrame#UserMessage{background:%5;border-color:%6;} QFrame#AssistantMessage{background:%3;} "
      "QTextBrowser{background:transparent;border:0;color:%2;selection-background-color:#244b78;} QTextBrowser a{color:#58a6ff;} QTextBrowser pre{background:%1;border:1px solid %6;border-radius:7px;padding:9px;} QTextBrowser code{font-family:Consolas,monospace;} "
      "QCheckBox{spacing:8px;} QCheckBox::indicator{width:15px;height:15px;border:1px solid %6;border-radius:4px;background:%3;} "
      "QCheckBox::indicator:checked{background:#58a6ff;border-color:#58a6ff;} "
      "QPushButton:disabled{color:%4;background:%3;border-color:%6;} "
      "QComboBox QAbstractItemView{background:%3;border:1px solid %6;selection-background-color:#244b78;} "
      "QSpinBox::up-button,QSpinBox::down-button{width:18px;border:0;background:transparent;} "
      "QToolTip{background:%3;color:%2;border:1px solid %6;padding:5px;}").arg(bg,fg,panel,muted,raised,border);
}
QPushButton *button(const QString &text,const QString &name,QWidget *parent) {
    auto *b=new QPushButton(text,parent); b->setObjectName(name); b->setCursor(Qt::PointingHandCursor); return b;
}
}
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent) {
    resize(1280,820); setMinimumSize(880,600); buildUi();
    connect(&m_ollama,&OllamaClient::modelsReceived,this,[this](const QStringList &models){
        const QString old=m_models->currentText(); m_models->clear(); m_models->addItems(models);
        if(models.isEmpty()) m_models->addItem("No models installed");
        else {
            QSettings settings;
            const QString preferred=settings.value("defaultModel",old).toString();
            int i=m_models->findText(preferred);
            if(i<0)i=m_models->findText(old);
            if(i>=0)m_models->setCurrentIndex(i);
        }
        m_models->setEnabled(!models.isEmpty());
    });
    connect(&m_ollama,&OllamaClient::connectionChanged,this,[this](bool ok,const QString &msg){
        m_status->setText(QString("●  %1").arg(msg));
        m_status->setStyleSheet(QString("color:%1;font-size:11px;").arg(ok?"#43c78b":"#e58c80"));
    });
    connect(&m_ollama,&OllamaClient::tokenReceived,this,[this](const QString &part){
        if(m_messages.isEmpty()||m_messages.last().role!="assistant")m_messages.append(Message{"assistant", QString{}});
        m_messages.last().content+=part;
        if(m_streamView)m_streamView->setMarkdown(m_messages.last().content);
        m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
    });
    connect(&m_ollama,&OllamaClient::thinkingReceived,this,[this](const QString &){
        if(m_streamView && m_thinking->isChecked())m_streamView->setToolTip("This model is producing a thinking trace.");
    });
    connect(&m_ollama,&OllamaClient::errorOccurred,this,[this](const QString &msg){
        m_status->setText(QString("●  %1").arg(msg)); m_status->setStyleSheet("color:#e58c80;font-size:11px;");
    });
    connect(&m_ollama,&OllamaClient::chatFinished,this,[this](){
        m_busy=false; m_send->setEnabled(true); m_send->setText("Send  ↗"); m_input->setEnabled(true);
        if(m_streamView && !m_messages.isEmpty() && m_messages.last().content.isEmpty())m_streamView->setMarkdown("*No visible response returned.*");
        m_streamView=nullptr;
        saveCurrentConversation();
        rebuildHistory();
    });
    QSettings settings;
    const QString theme=settings.value("theme","system").toString();
    if(theme=="dark")applyTheme(true);
    else if(theme=="light")applyTheme(false);
    else applyTheme(QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Light);
    connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,[this](Qt::ColorScheme scheme){
        QSettings s;
        if(s.value("theme","system").toString()=="system")applyTheme(scheme!=Qt::ColorScheme::Light);
    });
    QSettings settings;
    m_temperature->setValue(qRound(settings.value("temperature",0.7).toDouble()*10.0));
    m_context->setValue(settings.value("contextLength",8192).toInt());
    m_thinking->setChecked(settings.value("thinking",false).toBool());
    connect(m_temperature,&QSlider::valueChanged,this,[](int v){QSettings().setValue("temperature",v/10.0);});
    connect(m_context,qOverload<int>(&QSpinBox::valueChanged),this,[](int v){QSettings().setValue("contextLength",v);});
    connect(m_thinking,&QCheckBox::toggled,this,[](bool v){QSettings().setValue("thinking",v);});
    rebuildHistory(); refreshModels(); showWelcome();
}
void MainWindow::buildUi() {
    auto *root=new QWidget(this); auto *outer=new QHBoxLayout(root); outer->setContentsMargins(0,0,0,0); outer->setSpacing(0);
    auto *side=new QFrame(root); side->setObjectName("Sidebar"); side->setFixedWidth(260);
    auto *sl=new QVBoxLayout(side); sl->setContentsMargins(17,22,17,18); sl->setSpacing(12);
    auto *brandrow=new QHBoxLayout; auto *symbol=new QLabel("◐",side); symbol->setStyleSheet("color:#58a6ff;font-size:27px;font-weight:700;");
    auto *brand=new QLabel("ECLIPSE INFER",side); brand->setObjectName("Brand"); brandrow->addWidget(symbol);brandrow->addWidget(brand,1);sl->addLayout(brandrow);
    auto *tag=new QLabel("YOUR LOCAL AI WORKSPACE",side);tag->setObjectName("Eyebrow");sl->addWidget(tag);sl->addSpacing(10);
    auto *newButton=button("＋   New chat","Primary",side);sl->addWidget(newButton);connect(newButton,&QPushButton::clicked,this,&MainWindow::newChat);
    auto *recent=new QLabel("RECENT",side);recent->setObjectName("Eyebrow");sl->addWidget(recent);
    auto *historyScroll=new QScrollArea(side);historyScroll->setWidgetResizable(true);historyScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *historyHost=new QWidget(historyScroll);m_historyLayout=new QVBoxLayout(historyHost);m_historyLayout->setContentsMargins(0,0,0,0);m_historyLayout->addStretch();historyScroll->setWidget(historyHost);sl->addWidget(historyScroll,1);
    auto *settings=new QLabel("QUICK SETTINGS",side);settings->setObjectName("Eyebrow");sl->addWidget(settings);
    auto *tempRow=new QHBoxLayout;tempRow->addWidget(new QLabel("Temperature",side));tempRow->addStretch();auto *tempVal=new QLabel("0.7",side);tempVal->setObjectName("Muted");tempRow->addWidget(tempVal);sl->addLayout(tempRow);
    m_temperature=new QSlider(Qt::Horizontal,side);m_temperature->setRange(0,20);m_temperature->setValue(7);sl->addWidget(m_temperature);
    connect(m_temperature,&QSlider::valueChanged,this,[tempVal](int v){tempVal->setText(QString::number(v/10.0,'f',1));});
    auto *ctxRow=new QHBoxLayout;ctxRow->addWidget(new QLabel("Context length",side));ctxRow->addStretch();m_context=new QSpinBox(side);m_context->setRange(2048,131072);m_context->setSingleStep(2048);m_context->setValue(8192);m_context->setMaximumWidth(105);ctxRow->addWidget(m_context);sl->addLayout(ctxRow);
    m_thinking=new QCheckBox("Enable model thinking",side);m_thinking->setChecked(false);sl->addWidget(m_thinking);
    auto *bottom=new QHBoxLayout;m_theme=button("◐  Theme","Ghost",side);bottom->addWidget(m_theme);auto *refresh=button("↻  Refresh","Ghost",side);bottom->addWidget(refresh);sl->addLayout(bottom);
    auto *settingsButton=button("⚙  Settings","Ghost",side);sl->addWidget(settingsButton);
    connect(settingsButton,&QPushButton::clicked,this,&MainWindow::showSettings);
    connect(m_theme,&QPushButton::clicked,this,[this](){applyTheme(!m_dark);QSettings s;s.setValue("theme",m_dark?"dark":"light");});
    connect(refresh,&QPushButton::clicked,this,&MainWindow::refreshModels);outer->addWidget(side);
    auto *main=new QWidget(root);auto *ml=new QVBoxLayout(main);ml->setContentsMargins(28,18,28,20);ml->setSpacing(13);
    auto *top=new QHBoxLayout;auto *mlabel=new QLabel("MODEL",main);mlabel->setObjectName("Eyebrow");top->addWidget(mlabel);
    m_models=new QComboBox(main);m_models->setMinimumWidth(220);m_models->setMaximumWidth(360);top->addWidget(m_models);
    m_status=new QLabel("●  Connecting to Ollama…",main);m_status->setObjectName("Muted");top->addStretch();top->addWidget(m_status);ml->addLayout(top);
    m_chatTitle=new QLabel("New conversation",main);m_chatTitle->setStyleSheet("font-size:13px;font-weight:600;");ml->addWidget(m_chatTitle);
    m_scroll=new QScrollArea(main);m_scroll->setWidgetResizable(true);m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageHost=new QWidget(m_scroll);m_messageLayout=new QVBoxLayout(m_messageHost);m_messageLayout->setContentsMargins(0,8,0,8);m_messageLayout->setSpacing(14);m_messageLayout->addStretch();m_scroll->setWidget(m_messageHost);ml->addWidget(m_scroll,1);
    auto *composer=new QFrame(main);composer->setObjectName("Composer");auto *cl=new QVBoxLayout(composer);cl->setContentsMargins(12,10,12,10);
    m_input=new QLineEdit(composer);m_input->setPlaceholderText("Message your local model…");m_input->setMinimumHeight(42);cl->addWidget(m_input);
    auto *cb=new QHBoxLayout;auto *hint=new QLabel("Local inference · No cloud API key required",composer);hint->setObjectName("Muted");cb->addWidget(hint,1);
    m_send=button("Send  ↗","Primary",composer);cb->addWidget(m_send);cl->addLayout(cb);ml->addWidget(composer);
    connect(m_send,&QPushButton::clicked,this,&MainWindow::sendMessage);connect(m_input,&QLineEdit::returnPressed,this,&MainWindow::sendMessage);
    outer->addWidget(main,1);setCentralWidget(root);
}
void MainWindow::applyTheme(bool dark){m_dark=dark;qApp->setStyleSheet(appStyleSheet(dark));m_theme->setText(dark?"☼  Light mode":"◐  Dark mode");}
void MainWindow::refreshModels(){m_status->setText("●  Checking Ollama…");m_ollama.refreshModels();}
void MainWindow::addMessageCard(const QString &role,const QString &content,bool streaming) {
    auto *card=new QFrame(m_messageHost);card->setObjectName(role=="user"?"UserMessage":"AssistantMessage");
    card->setProperty("messageRole",role);
    auto *layout=new QVBoxLayout(card);layout->setContentsMargins(18,14,18,14);layout->setSpacing(9);
    auto *label=new QLabel((role=="user"?"YOU":"ECLIPSE INFER"),card);label->setStyleSheet(QString("font-size:10px;font-weight:800;letter-spacing:1px;color:%1;").arg(role=="user"?"#58a6ff":"#8d9bb0"));layout->addWidget(label);
    auto *view=new QTextBrowser(card);view->setReadOnly(true);view->setOpenExternalLinks(true);view->setMarkdown(content);view->setMinimumHeight(28);view->setFrameShape(QFrame::NoFrame);
    view->setStyleSheet("QTextBrowser{background:transparent;border:0;padding:0;}");layout->addWidget(view);
    m_messageLayout->insertWidget(m_messageLayout->count()-1,card);if(streaming)m_streamView=view;
}
void MainWindow::sendMessage() {
    if(m_busy)return;const QString prompt=m_input->text().trimmed();if(prompt.isEmpty())return;
    if(m_models->currentText().isEmpty()||m_models->currentText()=="No models installed"){QMessageBox::information(this,"No model selected","Install a model in Ollama, then press Refresh.");return;}
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
    m_messages.append(Message{"user", prompt});addMessageCard("user",prompt);m_input->clear();
    addMessageCard("assistant","*Generating…*",true);m_busy=true;m_send->setEnabled(false);m_send->setText("Generating…");m_input->setEnabled(false);
    m_ollama.chat(m_models->currentText(),apiMessages(),m_thinking->isChecked(),m_temperature->value()/10.0,m_context->value());
}
void MainWindow::newChat() {
    if(m_busy)m_ollama.cancel();
    saveCurrentConversation();
    m_messages.clear();m_title.clear();m_conversationId.clear();m_streamView=nullptr;m_busy=false;m_send->setEnabled(true);m_send->setText("Send  ↗");m_input->setEnabled(true);m_chatTitle->setText("New conversation");
    while(m_messageLayout->count()>1){auto *item=m_messageLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}showWelcome();rebuildHistory();
}
void MainWindow::showWelcome() {
    if(!m_messages.isEmpty()||m_messageLayout->count()>1)return;
    auto *w=new QWidget(m_messageHost);auto *l=new QVBoxLayout(w);l->setContentsMargins(12,65,12,45);l->setSpacing(13);l->setAlignment(Qt::AlignHCenter|Qt::AlignVCenter);
    auto *mark=new QLabel("◉",w);mark->setAlignment(Qt::AlignCenter);mark->setStyleSheet("font-size:52px;color:#58a6ff;");l->addWidget(mark);
    auto *title=new QLabel("Your AI. Your machine.",w);title->setAlignment(Qt::AlignCenter);title->setStyleSheet("font-size:30px;font-weight:650;");l->addWidget(title);
    auto *sub=new QLabel("A quieter, more focused home for your local models.",w);sub->setAlignment(Qt::AlignCenter);sub->setStyleSheet("color:#8d9bb0;font-size:14px;");l->addWidget(sub);
    auto *hint=new QLabel("Choose a model above and ask it anything.",w);hint->setAlignment(Qt::AlignCenter);hint->setStyleSheet("color:#8d9bb0;font-size:11px;");l->addWidget(hint);m_messageLayout->insertWidget(0,w);
}
QJsonArray MainWindow::apiMessages() const {
    QJsonArray a;for(const auto &m:m_messages){QJsonObject o;o.insert("role",m.role);o.insert("content",m.content);a.append(o);}return a;
}
void MainWindow::saveCurrentConversation() {
    if(m_conversationId.isEmpty()||m_messages.isEmpty())return;
    QJsonArray messages;
    for(const auto &message:m_messages){QJsonObject item;item.insert("role",message.role);item.insert("content",message.content);messages.append(item);}
    QSettings settings;
    settings.setValue("conversations/"+m_conversationId+"/title",m_title.isEmpty()?"New conversation":m_title);
    settings.setValue("conversations/"+m_conversationId+"/messages",QString::fromUtf8(QJsonDocument(messages).toJson(QJsonDocument::Compact)));
    settings.setValue("conversations/"+m_conversationId+"/model",m_models->currentText());
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
    const QString model=settings.value("conversations/"+id+"/model").toString();int modelIndex=m_models->findText(model);if(modelIndex>=0)m_models->setCurrentIndex(modelIndex);
    while(m_messageLayout->count()>1){auto *item=m_messageLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}
    for(const auto &value:document.array()){
        const auto object=value.toObject();const QString role=object.value("role").toString();const QString content=object.value("content").toString();
        if(role!="user"&&role!="assistant")continue;
        m_messages.append(Message{role,content});addMessageCard(role,content);
    }
    m_chatTitle->setText(m_title);m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}
void MainWindow::showSettings() {
    QDialog dialog(this);dialog.setWindowTitle("Eclipse Infer Settings");dialog.setMinimumWidth(420);
    auto *layout=new QVBoxLayout(&dialog);auto *form=new QFormLayout;
    auto *theme=new QComboBox(&dialog);theme->addItem("Follow system","system");theme->addItem("Dark","dark");theme->addItem("Light","light");
    QSettings settings;int themeIndex=theme->findData(settings.value("theme","system"));if(themeIndex>=0)theme->setCurrentIndex(themeIndex);
    auto *defaultModel=new QComboBox(&dialog);for(int i=0;i<m_models->count();++i)if(m_models->itemText(i)!="No models installed")defaultModel->addItem(m_models->itemText(i));
    const QString preferred=settings.value("defaultModel",m_models->currentText()).toString();int modelIndex=defaultModel->findText(preferred);if(modelIndex>=0)defaultModel->setCurrentIndex(modelIndex);
    auto *historyLimit=new QComboBox(&dialog);historyLimit->addItem("10 conversations",10);historyLimit->addItem("20 conversations",20);historyLimit->addItem("30 conversations",30);
    int limitIndex=historyLimit->findData(settings.value("historyLimit",30));if(limitIndex>=0)historyLimit->setCurrentIndex(limitIndex);
    auto *clearHistory=button("Delete all saved conversations","", &dialog);
    form->addRow("Appearance",theme);form->addRow("Default model",defaultModel);form->addRow("Keep recent chats",historyLimit);layout->addLayout(form);
    auto *privacy=new QLabel("Chats and preferences are stored locally in this app's settings. Prompts are sent to your local Ollama server.",&dialog);privacy->setWordWrap(true);privacy->setObjectName("Muted");layout->addWidget(privacy);layout->addWidget(clearHistory);
    auto *buttons=new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    connect(clearHistory,&QPushButton::clicked,&dialog,[this,&dialog](){if(QMessageBox::question(&dialog,"Delete saved chats","Permanently delete all saved conversation history?")==QMessageBox::Yes){QSettings s;const auto ids=s.value("conversationIds").toStringList();for(const auto &id:ids)s.remove("conversations/"+id);s.remove("conversationIds");m_historyIds.clear();rebuildHistory();}});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[this,&dialog,theme,defaultModel,historyLimit](){
        QSettings s;s.setValue("theme",theme->currentData().toString());s.setValue("defaultModel",defaultModel->currentText());s.setValue("historyLimit",historyLimit->currentData().toInt());
        applyTheme(theme->currentData().toString()=="light"?false:theme->currentData().toString()=="dark"?true:QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Light);
        dialog.accept();
    });
    dialog.exec();
}
