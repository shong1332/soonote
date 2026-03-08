#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QSqlDatabase>
#include <QCryptographicHash>

#ifdef Q_OS_MAC
#include <Carbon/Carbon.h>
#endif

#include "firebasemanager.h"
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onManualPull();
    void onFullUpload();
    void onFullDownload();
    void onOpenSettings();
    void onFileChanged(const QString &path);
    void onDirectoryChanged(const QString &path);
    void onDebounceTimeout();

private:
    // Tray
    QSystemTrayIcon *trayIcon;
    QMenu           *trayMenu;

    // 파일 감시
    QFileSystemWatcher *fileWatcher;

    // 디바운스 타이머 (10초)
    QTimer          *debounceTimer;
    QString          pendingFilePath;

    // DB
    QSqlDatabase     db;

    // 초기화 함수들
    void initTray();
    void initFileWatcher();
    void initDatabase();
    void initConfig();
    bool isConfigExists();
    void createSyncnoteFolder();


    // 설정
    QString watchFolderPath;
    QString firebaseApiKey;
    QString firebaseProjectId;

    void loadConfig();
    void saveConfig(const QString &watchFolder,
                    const QString &apiKey,
                    const QString &projectId);
    QString getConfigPath();

    void registerWatchFolder(const QString &folderPath);
    void registerAllFiles(const QString &folderPath);
    bool isTextFile(const QString &filePath);

    QByteArray compressFile(const QString &filePath);
    bool decompressToFile(const QByteArray &compressed, const QString &targetPath);

    FirebaseManager *firebaseManager;
    void initFirebase();

    QString calculateFileHash(const QString &filePath);
    void updateLocalMetadata(const QString &filePath, const QString &hash, qint64 fileSize);
    QString getLocalMetadata(const QString &filePath);

    void performPull();
    void backupFile(const QString &filePath);

    void handleFileDeleted(const QString &filePath);

    void performFullUpload();
    void performFullDownload();

    void registerGlobalHotKey();

    QAction *statusAction;
    void updateTrayStatus(const QString &status);

    void registerAutoStart();
    void unregisterAutoStart();

    bool checkFolderPermission(const QString &folderPath);
    void scanAndPushChanges();

    void testFirebaseConnection();
    QString getLocalModifiedTime(const QString &filePath);
};

#endif // MAINWINDOW_H
