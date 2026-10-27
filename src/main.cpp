#include "mainwindow.h"
#include <QApplication>
#include <QFontDatabase>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("Eclipse Infer");
    #ifndef ECLIPSE_INFER_VERSION
#define ECLIPSE_INFER_VERSION "0.1.0"
#endif
    QApplication::setApplicationVersion(ECLIPSE_INFER_VERSION);
    QApplication::setOrganizationName("Eclipse");
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(10);
    app.setFont(font);
    MainWindow window;
    window.show();
    return app.exec();
}
