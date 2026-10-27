#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("shakal"));
    // Wayland derives the app id / task-bar icon from this, not from the .desktop file's name.
    app.setDesktopFileName(QStringLiteral("shakal"));

    MainWindow window;
    window.show();

    return app.exec();
}
