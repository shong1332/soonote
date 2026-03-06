#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 트레이 앱은 마지막 창 닫혀도 종료 안 함
    a.setQuitOnLastWindowClosed(false);

    MainWindow w;
    // w.show() 제거 (백그라운드 앱)

    return a.exec();
}
