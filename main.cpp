#include <QApplication>
#include <QLockFile>
#include <QDir>
#include <QMessageBox>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);

    // 중복 실행 방지
    QLockFile lockFile(QDir::tempPath() + "/soonote.lock");
    if (!lockFile.tryLock(100)) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("SooNote");
        msgBox.setText("SooNote가 이미 실행 중입니다.");
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        return 1;
    }

    MainWindow w;
    return a.exec();
}
