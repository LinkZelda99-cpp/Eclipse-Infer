#include "mainwindow.h"
#include <QApplication>
#include <QFontDatabase>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Eclipse Infer");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("Eclipse");
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(10);
    app.setFont(font);
    MainWindow window;
    window.show();
    return app.exec();
}
