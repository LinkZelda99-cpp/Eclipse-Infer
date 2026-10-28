#include "mainwindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QSettings>
int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    // Preserve preferences and chat history created by Eclipse Infer.
    QSettings oldSettings("Eclipse", "Eclipse Infer");
    QSettings newSettings("Eclipse", "Eclipse Orbit");
    if (newSettings.allKeys().isEmpty() && !oldSettings.allKeys().isEmpty()) {
        for (const QString &key : oldSettings.allKeys())
            newSettings.setValue(key, oldSettings.value(key));
        newSettings.sync();
    }
    QApplication::setApplicationName("Eclipse Orbit");
    #ifndef ECLIPSE_ORBIT_VERSION
#define ECLIPSE_ORBIT_VERSION "0.1.0"
#endif
    QApplication::setApplicationVersion(ECLIPSE_ORBIT_VERSION);
    QApplication::setOrganizationName("Eclipse");
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPointSize(10);
    app.setFont(font);
    MainWindow window;
    window.show();
    return app.exec();
}
