QT += core gui widgets sql network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    settingsdialog.cpp \
    firebasemanager.cpp

HEADERS += \
    mainwindow.h \
    settingsdialog.h \
    firebasemanager.h
FORMS += \
    mainwindow.ui \
    settingsdialog.ui

TRANSLATIONS += \
    soonote_ko_KR.ts
CONFIG += lrelease
CONFIG += embed_translations

# Default rules for deployment.
macx: LIBS += -framework Carbon
win32: LIBS += -luser32
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
