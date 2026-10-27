#include "mainwindow.h"
#include <QApplication>
#include <QCheckBox>
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
#include <QVBoxLayout>
namespace {
QString style(bool dark) {
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
      "QFrame#MessageCard{background:%3;border:1px solid %6;border-radius:13px;} QTextBrowser{background:transparent;border:0;color:%2;}"
      "QCheckBox{spacing:8px;}").arg(bg,fg,panel,muted,raised,border);
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
        else { const int i=m_models->findText(old); if(i>=0)m_models->setCurrentIndex(i); }
        m_models->setEnabled(!models.isEmpty());
    });
    connect(&m_ollama,&OllamaClient::connectionChanged,this,[this](bool ok,const QString &msg){
        m_status->setText(QString("●  %1").arg(msg));
        m_status->setStyleSheet(QString("color:%1;font-size:11px;").arg(ok?"#43c78b":"#e58c80"));
    });
    connect(&m_ollama,&OllamaClient::tokenReceived,this,[this](const QString &part){
        if(m_messages.isEmpty()||m_messages.last().role!="assistant")m_messages.append({"assistant",{}});
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
        if(!m_title.isEmpty()){ QSettings s; QStringList list=s.value("history").toStringList(); list.removeAll(m_title); list.prepend(m_title); while(list.size()>12)list.removeLast(); s.setValue("history",list); }
        rebuildHistory();
    });
    QSettings settings;
    const QString theme=settings.value("theme","system").toString();
    if(theme=="dark")applyTheme(true);
    else if(theme=="light")applyTheme(false);
    else {
        applyTheme(QGuiApplication::styleHints()->colorScheme()!=Qt::ColorScheme::Light);
        connect(QGuiApplication::styleHints(),&QStyleHints::colorSchemeChanged,this,[this](Qt::ColorScheme c){
            QSettings s; if(s.value("theme","system").toString()=="system")applyTheme(c!=Qt::ColorScheme::Light);
        });
    }
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
void MainWindow::applyTheme(bool dark){m_dark=dark;qApp->setStyleSheet(style(dark));m_theme->setText(dark?"☼  Light mode":"◐  Dark mode");}
void MainWindow::refreshModels(){m_status->setText("●  Checking Ollama…");m_ollama.refreshModels();}
void MainWindow::addMessageCard(const QString &role,const QString &content,bool streaming) {
    auto *card=new QFrame(m_messageHost);card->setObjectName("MessageCard");auto *layout=new QVBoxLayout(card);layout->setContentsMargins(16,12,16,12);layout->setSpacing(7);
    auto *label=new QLabel((role=="user"?"YOU":"ECLIPSE INFER"),card);label->setStyleSheet("font-size:11px;font-weight:700;color:#8d9bb0;");layout->addWidget(label);
    auto *view=new QTextBrowser(card);view->setReadOnly(true);view->setOpenExternalLinks(true);view->setMarkdown(content);view->setMinimumHeight(28);layout->addWidget(view);
    m_messageLayout->insertWidget(m_messageLayout->count()-1,card);if(streaming)m_streamView=view;
}
void MainWindow::sendMessage() {
    if(m_busy)return;const QString prompt=m_input->text().trimmed();if(prompt.isEmpty())return;
    if(m_models->currentText().isEmpty()||m_models->currentText()=="No models installed"){QMessageBox::information(this,"No model selected","Install a model in Ollama, then press Refresh.");return;}
    if(m_title.isEmpty()){m_title=prompt.simplified();if(m_title.size()>34)m_title=m_title.left(31)+"…";m_chatTitle->setText(m_title);}
    m_messages.append({"user",prompt});addMessageCard("user",prompt);m_input->clear();
    addMessageCard("assistant","*Generating…*",true);m_busy=true;m_send->setEnabled(false);m_send->setText("Generating…");m_input->setEnabled(false);
    m_ollama.chat(m_models->currentText(),apiMessages(),m_thinking->isChecked(),m_temperature->value()/10.0,m_context->value());
}
void MainWindow::newChat() {
    if(m_busy)m_ollama.cancel();m_messages.clear();m_title.clear();m_streamView=nullptr;m_busy=false;m_send->setEnabled(true);m_send->setText("Send  ↗");m_input->setEnabled(true);m_chatTitle->setText("New conversation");
    while(m_messageLayout->count()>1){auto *item=m_messageLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}showWelcome();
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
void MainWindow::rebuildHistory() {
    while(m_historyLayout->count()>1){auto *item=m_historyLayout->takeAt(0);if(item->widget())item->widget()->deleteLater();delete item;}
    QSettings s;const QStringList list=s.value("history").toStringList();
    for(const auto &title:list){auto *b=button("◦  "+title,"Ghost",this);b->setToolTip(title);m_historyLayout->insertWidget(m_historyLayout->count()-1,b);
      connect(b,&QPushButton::clicked,this,[this,title](){QMessageBox::information(this,"Conversation history","The title is saved, but reopening full transcripts is coming in a future update.\n\n"+title);});}
}
