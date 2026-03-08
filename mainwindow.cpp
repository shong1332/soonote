#include "mainwindow.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <QSqlQuery>
#include <QSqlError>
#include <QDirIterator>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QStyle>
#include <QMessageBox>

#include "settingsdialog.h"
#include "firebasemanager.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    this->hide();

    initDatabase();
    initTray();
    initFileWatcher();
    initFirebase();
    registerAutoStart();  // ← 추가

    QTimer::singleShot(0, this, &MainWindow::initConfig);

    qDebug("SooNote engine started");
}


MainWindow::~MainWindow()
{
    db.close();
    qDebug("SooNote engine stopped");
}

// ───────────────────────────────
// 설정 파일 초기화
// ───────────────────────────────
void MainWindow::initConfig()
{
    createSyncnoteFolder();

    if (!isConfigExists()) {
        qDebug("Config not found. First run detected");

        SettingsDialog dialog;
        dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowStaysOnTopHint);
        dialog.raise();
        dialog.activateWindow();

        if (dialog.exec() == QDialog::Accepted) {
            watchFolderPath   = dialog.getWatchFolder();
            firebaseApiKey    = dialog.getApiKey();
            firebaseProjectId = dialog.getProjectId();

            saveConfig(watchFolderPath, firebaseApiKey, firebaseProjectId);
            firebaseManager->setConfig(firebaseApiKey, firebaseProjectId);

            if (!checkFolderPermission(watchFolderPath)) {
                watchFolderPath.clear();
                saveConfig("", firebaseApiKey, firebaseProjectId);
                QTimer::singleShot(0, this, &MainWindow::initConfig);
                return;
            }
            registerWatchFolder(watchFolderPath);
            qDebug("First run setup completed");
            QTimer::singleShot(500, this, &MainWindow::performPull);
            QTimer::singleShot(1000, this, &MainWindow::scanAndPushChanges);
        }
    } else {
        loadConfig();

        // watch_folder 비어있으면 설정 다이얼로그 띄우기
        if (watchFolderPath.isEmpty()) {
            qDebug("Watch folder empty. Showing settings dialog");
            SettingsDialog dialog;
            dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowStaysOnTopHint);
            dialog.raise();
            dialog.activateWindow();

            if (dialog.exec() == QDialog::Accepted) {
                watchFolderPath   = dialog.getWatchFolder();
                firebaseApiKey    = dialog.getApiKey();
                firebaseProjectId = dialog.getProjectId();
                saveConfig(watchFolderPath, firebaseApiKey, firebaseProjectId);
            } else {
                QTimer::singleShot(0, qApp, &QApplication::quit);
                return;
            }
        }

        firebaseManager->setConfig(firebaseApiKey, firebaseProjectId);

        if (!checkFolderPermission(watchFolderPath)) {
            watchFolderPath.clear();
            saveConfig("", firebaseApiKey, firebaseProjectId);
            QTimer::singleShot(0, this, &MainWindow::initConfig);
            return;
        }
        registerWatchFolder(watchFolderPath);
        qDebug("Config loaded successfully");
        QTimer::singleShot(500, this, &MainWindow::performPull);
        QTimer::singleShot(1000, this, &MainWindow::scanAndPushChanges);
    }
}

void MainWindow::createSyncnoteFolder()
{
    // 임시로 홈 디렉토리 안에 생성 (나중에 사용자 지정 폴더로 변경)
    QString syncnotePath = QDir::homePath() + "/.syncnote";
    QDir dir;
    if (!dir.exists(syncnotePath)) {
        dir.mkpath(syncnotePath);
        qDebug("Created .syncnote folder at %s", qPrintable(syncnotePath));
    } else {
        qDebug(".syncnote folder already exists");
    }
}

bool MainWindow::isConfigExists()
{
    QString configPath = QDir::homePath() + "/.syncnote/config.json";
    return QFile::exists(configPath);
}

// ───────────────────────────────
// SQLite 초기화
// ───────────────────────────────
void MainWindow::initDatabase()
{
    QString dbPath = QDir::homePath() + "/.syncnote/metadata.db";
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);

    if (!db.open()) {
        qDebug("Failed to open database");
        return;
    }

    QSqlQuery query;
    query.exec(
        "CREATE TABLE IF NOT EXISTS file_metadata ("
        "file_path     TEXT PRIMARY KEY,"
        "file_hash     TEXT NOT NULL,"
        "last_modified TEXT NOT NULL,"
        "file_size     INTEGER NOT NULL"
        ")"
        );

    qDebug("Database initialized successfully");
}

// ───────────────────────────────
// Tray 초기화
// ───────────────────────────────
void MainWindow::initTray()
{
    trayMenu = new QMenu(this);

    statusAction = new QAction("SooNote - 대기 중", this);  // ← 멤버변수로
    statusAction->setEnabled(false);
    trayMenu->addAction(statusAction);
    trayMenu->addSeparator();


    QAction *pullAction = new QAction("지금 Pull", this);
    connect(pullAction, &QAction::triggered, this, &MainWindow::onManualPull);
    trayMenu->addAction(pullAction);
    trayMenu->addSeparator();

    QMenu *fullSyncMenu = trayMenu->addMenu("Full Sync");
    QAction *uploadAction = new QAction("전체 업로드", this);
    QAction *downloadAction = new QAction("전체 다운로드", this);
    connect(uploadAction, &QAction::triggered, this, &MainWindow::onFullUpload);
    connect(downloadAction, &QAction::triggered, this, &MainWindow::onFullDownload);
    fullSyncMenu->addAction(uploadAction);
    fullSyncMenu->addAction(downloadAction);
    trayMenu->addSeparator();

    QAction *settingsAction = new QAction("설정", this);
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
    trayMenu->addAction(settingsAction);
    trayMenu->addSeparator();

    QAction *quitAction = new QAction("종료", this);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    trayMenu->addAction(quitAction);

    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setContextMenu(trayMenu); // ← 기본 컨텍스트 메뉴 제거
    trayIcon->setToolTip("SooNote");
    trayIcon->setIcon(QApplication::style()->standardIcon(QStyle::SP_ComputerIcon));
    trayIcon->show();

    connect(trayIcon, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayIconActivated);

    qDebug("Tray icon initialized");
}

// ───────────────────────────────
// 파일 감시 초기화
// ───────────────────────────────
void MainWindow::initFileWatcher()
{

    fileWatcher = new QFileSystemWatcher(this);
        qDebug("Watched directories: %s", qPrintable(fileWatcher->directories().join(", ")));
    debounceTimer = new QTimer(this);
    debounceTimer->setSingleShot(true);
    debounceTimer->setInterval(10000); // 10초

    // 5분마다 자동 Pull
    QTimer *autoPullTimer = new QTimer(this);
    autoPullTimer->setInterval(5 * 60 * 1000); // 5분
    connect(autoPullTimer, &QTimer::timeout, this, &MainWindow::performPull);
    autoPullTimer->start();
    qDebug("Auto pull timer started (5 min interval)");


    connect(fileWatcher, &QFileSystemWatcher::fileChanged,
            this, &MainWindow::onFileChanged);
    connect(fileWatcher, &QFileSystemWatcher::directoryChanged,
            this, &MainWindow::onDirectoryChanged);
    connect(debounceTimer, &QTimer::timeout,
            this, &MainWindow::onDebounceTimeout);

    // 설정에 감시 폴더가 있으면 바로 등록
    if (!watchFolderPath.isEmpty()) {
        registerWatchFolder(watchFolderPath);
    } else {
        qDebug("Watch folder not set. Waiting for settings");
    }

    qDebug("File watcher initialized");
}
// ───────────────────────────────
// Slots
// ───────────────────────────────

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason)
    // Mac은 setContextMenu로 자동 처리됨
}

void MainWindow::onManualPull()
{
    qDebug("Manual pull requested by user");
    performPull();
}

void MainWindow::onFullUpload()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("전체 업로드 확인");
    msgBox.setText("로컬 파일 전체를 Firebase에 업로드합니다.\n"
                   "Firebase의 기존 파일이 덮어씌워집니다.\n\n"
                   "계속하시겠습니까?");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);

    if (msgBox.exec() == QMessageBox::Yes) {
        performFullUpload();
    }
}

void MainWindow::onFullDownload()
{
    QMessageBox msgBox;
    msgBox.setWindowTitle("전체 다운로드 확인");
    msgBox.setText("Firebase 파일 전체를 로컬에 덮어씁니다.\n"
                   "로컬의 기존 파일이 .bak으로 백업됩니다.\n\n"
                   "계속하시겠습니까?");
    msgBox.setIcon(QMessageBox::Warning);
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);

    if (msgBox.exec() == QMessageBox::Yes) {
        performFullDownload();
    }
}

void MainWindow::onOpenSettings()
{
    qDebug("Settings opened by user");

    SettingsDialog dialog;
    dialog.setWindowFlags(dialog.windowFlags() | Qt::WindowStaysOnTopHint);
    dialog.setCurrentSettings(watchFolderPath, firebaseApiKey, firebaseProjectId);
    dialog.raise();
    dialog.activateWindow();

    if (dialog.exec() == QDialog::Accepted) {
        QString newWatchFolder   = dialog.getWatchFolder();
        QString newApiKey        = dialog.getApiKey();
        QString newProjectId     = dialog.getProjectId();

        // Firebase에 파일이 있으면 경고
        if (newWatchFolder != watchFolderPath) {
            QMessageBox msgBox;
            msgBox.setWindowTitle("폴더 변경 확인");
            msgBox.setText(
                "Firebase에 이미 동기화된 파일이 있을 수 있습니다.\n\n"
                "현재 선택한 폴더: " + newWatchFolder + "\n\n"
                                   "계속하면 이 폴더로 동기화됩니다.\n"
                                   "맞습니까?"
                );
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
            msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);

            if (msgBox.exec() != QMessageBox::Yes) {
                return; // 취소
            }
        }

        watchFolderPath   = newWatchFolder;
        firebaseApiKey    = newApiKey;
        firebaseProjectId = newProjectId;

        if (!checkFolderPermission(watchFolderPath)) {
            watchFolderPath = dialog.getWatchFolder();
            return;
        }

        saveConfig(watchFolderPath, firebaseApiKey, firebaseProjectId);
        firebaseManager->setConfig(firebaseApiKey, firebaseProjectId);
        fileWatcher->removePaths(fileWatcher->files());
        fileWatcher->removePaths(fileWatcher->directories());
        registerWatchFolder(watchFolderPath);
        QTimer::singleShot(200, this, &MainWindow::testFirebaseConnection);
        qDebug("Settings updated successfully");
    }
}

void MainWindow::onFileChanged(const QString &path)
{
    // 파일이 삭제된 경우 무시
    if (!QFile::exists(path)) {
        qDebug("File no longer exists, ignoring: %s", qPrintable(path));
        return;
    }

    qDebug("File change detected: %s", qPrintable(path));
    pendingFilePath = path;
    debounceTimer->start();
}

void MainWindow::onDirectoryChanged(const QString &path)
{
    qDebug("Directory change detected: %s", qPrintable(path));

    if (!QDir(path).exists()) {
        fileWatcher->removePaths(fileWatcher->files());
        fileWatcher->removePaths(fileWatcher->directories());
        registerWatchFolder(watchFolderPath);
        return;
    }

    // 해당 디렉토리의 직접 자식 파일만 검사 (서브폴더 제외)
    QStringList watchedFiles = fileWatcher->files();
    for (const QString &filePath : watchedFiles) {
        QFileInfo fi(filePath);
        if (fi.absolutePath() != path) continue; // ← 직접 자식만
        if (!QFile::exists(filePath)) {
            qDebug("File deleted detected: %s", qPrintable(filePath));
            handleFileDeleted(filePath);
        }
    }

    registerAllFiles(path);
}

void MainWindow::onDebounceTimeout()
{
    qDebug("Debounce timeout. Starting push for: %s", qPrintable(pendingFilePath));
    updateTrayStatus("업로드 중...");
    if (pendingFilePath.isEmpty()) {
        qDebug("No pending file. Skipping push");
        return;
    }

    // 파일 크기 제한 (50MB) ← 맨 위로 이동
    QFileInfo fileInfo(pendingFilePath);
    if (fileInfo.size() > 50 * 1024 * 1024) {
        qDebug("File too large. Skipping push: %s", qPrintable(pendingFilePath));
        QMessageBox msgBox;
        msgBox.setWindowTitle("파일 크기 초과");
        msgBox.setText("50MB 이상 파일은 동기화되지 않습니다.\n\n" + pendingFilePath);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        pendingFilePath.clear();
        updateTrayStatus("대기 중");
        return;
    }

    // 1. 현재 해시 계산
    QString currentHash = calculateFileHash(pendingFilePath);
    if (currentHash.isEmpty()) {
        qDebug("Failed to calculate hash. Skipping push");
        return;
    }

    // 2. 로컬 DB 해시와 비교
    QString savedHash = getLocalMetadata(pendingFilePath);
    if (currentHash == savedHash) {
        qDebug("File unchanged. Skipping push");
        return;
    }

    // 3. 파일 압축
    QByteArray compressed = compressFile(pendingFilePath);
    if (compressed.isEmpty()) {
        qDebug("Compression failed. Skipping push");
        return;
    }

    // 4. Firebase 경로 계산
    qDebug("watchFolderPath: %s", qPrintable(watchFolderPath));
    qDebug("pendingFilePath: %s", qPrintable(pendingFilePath));
    QString remotePath = "files/" + pendingFilePath.mid(watchFolderPath.length() + 1) + ".gz";
    qDebug("remotePath: %s", qPrintable(remotePath));

    // 5. Firebase 업로드
    firebaseManager->uploadFile(pendingFilePath, remotePath, compressed);



    firebaseManager->updateMetadata(
        remotePath,  // ← files/ 없이 그냥 경로만
        currentHash,
        QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
        fileInfo.size()
        );

    // 7. 로컬 DB 업데이트
    updateLocalMetadata(pendingFilePath, currentHash, fileInfo.size());

    pendingFilePath.clear();

       updateTrayStatus("동기화 완료");
}

// ───────────────────────────────
// 간단한 XOR 암호화/복호화
// ───────────────────────────────
static QString xorEncrypt(const QString &input)
{
    const QString key = "SooNote2026SecretKey!";
    QByteArray data = input.toUtf8();
    QByteArray keyData = key.toUtf8();
    for (int i = 0; i < data.size(); i++) {
        data[i] = data[i] ^ keyData[i % keyData.size()];
    }
    return QString::fromLatin1(data.toBase64());
}

static QString xorDecrypt(const QString &input)
{
    const QString key = "SooNote2026SecretKey!";
    QByteArray data = QByteArray::fromBase64(input.toLatin1());
    QByteArray keyData = key.toUtf8();
    for (int i = 0; i < data.size(); i++) {
        data[i] = data[i] ^ keyData[i % keyData.size()];
    }
    return QString::fromUtf8(data);
}

// ───────────────────────────────
// 설정 파일 관리
// ───────────────────────────────
QString MainWindow::getConfigPath()
{
    return QDir::homePath() + "/.syncnote/config.json";
}


void MainWindow::loadConfig()
{
    QFile file(getConfigPath());
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug("Failed to open config file");
        return;
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject obj = doc.object();
    firebaseApiKey    = xorDecrypt(obj["firebase_api_key"].toString());
    firebaseProjectId = xorDecrypt(obj["firebase_project_id"].toString());
    watchFolderPath   = obj["watch_folder"].toString();
    qDebug("Config loaded: watch_folder = %s", qPrintable(watchFolderPath));
}

void MainWindow::saveConfig(const QString &watchFolder,
                            const QString &apiKey,
                            const QString &projectId)
{
    QJsonObject obj;
    obj["firebase_api_key"]    = xorEncrypt(apiKey);
    obj["firebase_project_id"] = xorEncrypt(projectId);
    obj["watch_folder"]        = watchFolder;
    QJsonDocument doc(obj);
    QFile file(getConfigPath());
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug("Failed to save config file");
        return;
    }
    file.write(doc.toJson());
    qDebug("Config saved successfully");
}


// ───────────────────────────────
// 파일 감시 등록
// ───────────────────────────────
void MainWindow::registerWatchFolder(const QString &folderPath)
{
    QDir dir(folderPath);
    if (!dir.exists()) {
        qDebug("Watch folder does not exist: %s", qPrintable(folderPath));
        return;
    }

    // 루트 폴더 자체 감시 등록 ← 추가
    fileWatcher->addPath(folderPath);
    qDebug("Watching directory: %s", qPrintable(folderPath));

    registerAllFiles(folderPath);
    qDebug("Watch folder registered: %s", qPrintable(folderPath));
}

void MainWindow::registerAllFiles(const QString &folderPath)
{
    QDirIterator it(
        folderPath,
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories
        );

    while (it.hasNext()) {
        QString path = it.next();

        // 심볼릭 링크 제외 ← 추가
        if (QFileInfo(path).isSymLink()) {
            qDebug("Skipping symlink: %s", qPrintable(path));
            continue;
        }

        // .syncnote 폴더 제외
        if (path.contains("/.syncnote")) {
            continue;
        }

        // 폴더면 감시 등록
        if (QFileInfo(path).isDir()) {
            fileWatcher->addPath(path);
            qDebug("Watching directory: %s", qPrintable(path));
            continue;
        }

        // 텍스트 파일만 감시 등록
        if (isTextFile(path)) {
            fileWatcher->addPath(path);
            qDebug("Watching file: %s", qPrintable(path));
        }
    }
}
bool MainWindow::isTextFile(const QString &filePath)
{
    // 숨김 파일 제외 (.DS_Store, .gitignore 등)
    QFileInfo info(filePath);
    if (info.fileName().startsWith(".")) return false;

    // 숨김 폴더 제외
    QStringList parts = filePath.split("/");
    for (const QString &part : parts) {
        if (part.startsWith(".")) return false;
    }

    // 텍스트 파일 확장자 체크
    QStringList textExtensions = {
        "txt", "md", "log", "csv", "json", "xml",
        "html", "css", "js", "cpp", "h", "py",
        "ts", "jsx", "tsx", "vue", "swift", "kt"
    };

    QString ext = info.suffix().toLower();
    return textExtensions.contains(ext);
}

// ───────────────────────────────
// 압축 / 해제
// ───────────────────────────────
QByteArray MainWindow::compressFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug("Failed to open file: %s", qPrintable(filePath));
        return QByteArray();
    }

    QByteArray data = file.readAll();
    file.close();

    // 빈 파일 처리
    if (data.isEmpty()) {
        qDebug("Empty file, using empty compressed data: %s", qPrintable(filePath));
        return qCompress(QByteArray(""), 6); // 빈 데이터 압축
    }

    QByteArray compressed = qCompress(data, 6);
    qDebug("Compressed chunk: %d bytes", compressed.size());
    qDebug("Compression completed for: %s", qPrintable(filePath));
    return compressed;
}

bool MainWindow::decompressToFile(const QByteArray &compressed, const QString &targetPath)
{
    QByteArray decompressed = qUncompress(compressed);
    if (decompressed.isEmpty()) {
        qDebug("Decompression failed for: %s", qPrintable(targetPath));
        return false;
    }

    // 읽기 전용 파일 체크 ← 추가
    QFileInfo fileInfo(targetPath);
    if (fileInfo.exists() && !fileInfo.isWritable()) {
        qDebug("File is read-only, skipping: %s", qPrintable(targetPath));
        QMessageBox msgBox;
        msgBox.setWindowTitle("파일 쓰기 실패");
        msgBox.setText("읽기 전용 파일입니다. 동기화할 수 없습니다.\n\n" + targetPath);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        return false;
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly)) {
        qDebug("Failed to open file for writing: %s", qPrintable(targetPath));
        return false;
    }

    file.write(decompressed);
    file.close();
    qDebug("Decompression completed for: %s", qPrintable(targetPath));
    return true;
}

// ───────────────────────────────
// Firebase 초기화
// ───────────────────────────────
void MainWindow::initFirebase()
{
    firebaseManager = new FirebaseManager(this);

    if (!firebaseApiKey.isEmpty() && !firebaseProjectId.isEmpty()) {
        firebaseManager->setConfig(firebaseApiKey, firebaseProjectId);
        qDebug("Firebase initialized with config");
    } else {
        qDebug("Firebase config not set yet. Waiting for settings");
    }

    // Signal/Slot 연결
    connect(firebaseManager, &FirebaseManager::uploadFinished,
            this, [](const QString &path, bool success) {
                if (success)
                    qDebug("Upload finished: %s", qPrintable(path));
                else
                    qDebug("Upload failed: %s", qPrintable(path));
            });

    connect(firebaseManager, &FirebaseManager::downloadFinished,
            this, [](const QString &remotePath, const QString &localPath, bool success) {
                if (success)
                    qDebug("Download finished: %s to %s", qPrintable(remotePath), qPrintable(localPath));
                else
                    qDebug("Download failed: %s", qPrintable(remotePath));
            });
}


// ───────────────────────────────
// 파일 해시 계산
// ───────────────────────────────
QString MainWindow::calculateFileHash(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        qDebug("Failed to open file for hashing: %s", qPrintable(filePath));
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Md5);
    const qint64 CHUNK_SIZE = 1024 * 1024; // 1MB 청크

    while (!file.atEnd()) {
        QByteArray chunk = file.read(CHUNK_SIZE);
        hash.addData(chunk);
    }

    file.close();
    QString result = hash.result().toHex();
    qDebug("Hash calculated for %s: %s", qPrintable(filePath), qPrintable(result));
    return result;
}

// ───────────────────────────────
// 로컬 메타데이터 업데이트
// ───────────────────────────────
void MainWindow::updateLocalMetadata(const QString &filePath,
                                     const QString &hash,
                                     qint64 fileSize)
{
    QSqlQuery query;
    query.prepare(
        "INSERT OR REPLACE INTO file_metadata "
        "(file_path, file_hash, last_modified, file_size) "
        "VALUES (:path, :hash, :modified, :size)"
        );
    query.bindValue(":path",     filePath);
    query.bindValue(":hash",     hash);
    query.bindValue(":modified", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    query.bindValue(":size",     fileSize);

    if (query.exec()) {
        qDebug("Local metadata updated for: %s", qPrintable(filePath));
    } else {
        qDebug("Failed to update metadata for: %s", qPrintable(filePath));
    }
}

// ───────────────────────────────
// 로컬 메타데이터 조회
// ───────────────────────────────
QString MainWindow::getLocalMetadata(const QString &filePath)
{
    QSqlQuery query;
    query.prepare(
        "SELECT file_hash, last_modified FROM file_metadata "
        "WHERE file_path = :path"
        );
    query.bindValue(":path", filePath);

    if (query.exec() && query.next()) {
        QString hash     = query.value(0).toString();
        QString modified = query.value(1).toString();
        qDebug("Metadata found for %s: hash=%s modified=%s",
               qPrintable(filePath), qPrintable(hash), qPrintable(modified));
        return hash;
    }

    qDebug("No metadata found for: %s", qPrintable(filePath));
    return QString();
}


// ───────────────────────────────
// Pull 로직
// ───────────────────────────────
void MainWindow::performPull()
{
    updateTrayStatus("다운로드 중...");

    if (watchFolderPath.isEmpty()) {
        qDebug("Watch folder not set. Skipping pull");
        return;
    }

    if (!firebaseManager) {
        qDebug("Firebase not initialized. Skipping pull");
        return;
    }

    qDebug("Starting pull from Firebase");

    // Firestore에서 전체 메타데이터 가져오기
    connect(firebaseManager, &FirebaseManager::allMetadataFetched,
            this, [this](const QJsonObject &metadata) {

                if (metadata.isEmpty()) {
                    qDebug("No metadata from server. Skipping pull");
                    return;
                }

                QJsonArray documents = metadata["documents"].toArray();

                for (const QJsonValue &doc : documents) {
                    QJsonObject docObj = doc.toObject();
                    QJsonObject fields = docObj["fields"].toObject();

                    // 문서 ID에서 remotePath 추출 ← 수정
                    QString docName = docObj["name"].toString();
                    QString docId = docName.mid(docName.lastIndexOf("/") + 1);
                    QString remotePath = docId;
                    remotePath.replace("__", "/"); // __ → / 복원

                    QString serverHash   = fields["hash"].toObject()["stringValue"].toString();
                    QString lastModified = fields["lastModified"].toObject()["stringValue"].toString();

                    // 로컬 경로 계산
                    QString localPath = watchFolderPath + "/" +
                                        remotePath.mid(QString("files/").length());
                    localPath.remove(".gz");

                    QString localHash = getLocalMetadata(localPath);
                    if (localHash == serverHash) {
                        qDebug("File up to date: %s", qPrintable(localPath));
                        continue;
                    }

                    // LWW - 타임스탬프 비교 ← 추가
                    QString localModified = getLocalModifiedTime(localPath);
                    if (!localModified.isEmpty() && !lastModified.isEmpty()) {
                        QDateTime localTime = QDateTime::fromString(localModified, Qt::ISODate);
                        QDateTime serverTime = QDateTime::fromString(lastModified, Qt::ISODate);
                        if (localTime > serverTime) {
                            qDebug("Local is newer. Skipping download: %s", qPrintable(localPath));
                            continue;
                        }
                    }

                    // 서버가 더 최신이면 다운로드
                    qDebug("File outdated. Downloading: %s", qPrintable(localPath));
                    backupFile(localPath);
                    firebaseManager->downloadFile(remotePath, localPath);
                }
            },  Qt::UniqueConnection);

    firebaseManager->fetchAllMetadata();
}

// ───────────────────────────────
// .bak 백업
// ───────────────────────────────
void MainWindow::backupFile(const QString &filePath)
{
    if (!QFile::exists(filePath)) {
        return;
    }

    QString date = QDateTime::currentDateTime().toString("yyyyMMdd");
    QString basePath = filePath + "." + date;
    QString backupPath = basePath + ".bak";

    // 같은 날짜 .bak 최대 3개
    int count = 1;
    while (QFile::exists(backupPath) && count < 3) {
        backupPath = basePath + "_" + QString::number(count) + ".bak";
        count++;
    }

    if (QFile::copy(filePath, backupPath)) {
        qDebug("Backup created: %s", qPrintable(backupPath));
    } else {
        qDebug("Backup failed for: %s", qPrintable(filePath));
    }
}


// ───────────────────────────────
// 삭제 동기화
// ───────────────────────────────
void MainWindow::handleFileDeleted(const QString &filePath)
{
    if (!isTextFile(filePath)) return;

    // 1. Firebase Storage에서 삭제
    QString remotePath = "files/" + filePath.mid(watchFolderPath.length() + 1) + ".gz";
    firebaseManager->deleteFile(remotePath);

    // 2. Firestore 메타데이터 삭제
    firebaseManager->deleteMetadata(remotePath);

    // 3. 로컬 DB에서 삭제
    QSqlQuery query;
    query.prepare("DELETE FROM file_metadata WHERE file_path = :path");
    query.bindValue(":path", filePath);
    if (query.exec()) {
        qDebug("Local metadata deleted for: %s", qPrintable(filePath));
    }

    // 4. 파일 감시 해제
    fileWatcher->removePath(filePath);
    qDebug("File removed from watch list: %s", qPrintable(filePath));
}



// ───────────────────────────────
// Full Sync
// ───────────────────────────────
void MainWindow::performFullUpload()
{
    if (watchFolderPath.isEmpty()) {
        qDebug("Watch folder not set. Skipping full upload");
        return;
    }

    qDebug("Starting full upload");

    QDirIterator it(
        watchFolderPath,
        QDir::Files,
        QDirIterator::Subdirectories
        );

    while (it.hasNext()) {
        QString filePath = it.next();

        if (filePath.contains("/.syncnote")) continue;
        if (!isTextFile(filePath)) continue;

        QString currentHash = calculateFileHash(filePath);
        QByteArray compressed = compressFile(filePath);
        QString remotePath = "files/" + filePath.mid(watchFolderPath.length() + 1) + ".gz";
        QFileInfo fileInfo(filePath);

        firebaseManager->uploadFile(filePath, remotePath, compressed);
        firebaseManager->updateMetadata(
            remotePath,
            currentHash,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate),
            fileInfo.size()
            );
        updateLocalMetadata(filePath, currentHash, fileInfo.size());

        qDebug("Full upload: %s", qPrintable(filePath));
    }

    qDebug("Full upload completed");
}

void MainWindow::performFullDownload()
{
    if (watchFolderPath.isEmpty()) {
        qDebug("Watch folder not set. Skipping full download");
        return;
    }

    qDebug("Starting full download");

    connect(firebaseManager, &FirebaseManager::allMetadataFetched,
            this, [this](const QJsonObject &metadata) {

                if (metadata.isEmpty()) {
                    qDebug("No metadata from server. Skipping full download");
                    return;
                }

                QJsonArray documents = metadata["documents"].toArray();

                for (const QJsonValue &doc : documents) {
                    QJsonObject fields = doc.toObject()["fields"].toObject();

                    QString remotePath = fields["remotePath"].toObject()["stringValue"].toString();
                    QString serverHash = fields["hash"].toObject()["stringValue"].toString();

                    QString localPath = watchFolderPath + "/" +
                                        remotePath.mid(QString("files/").length());
                    localPath.remove(".gz");

                    backupFile(localPath);
                    firebaseManager->downloadFile(remotePath, localPath);

                    qDebug("Full download: %s", qPrintable(localPath));
                }

                qDebug("Full download completed");
            });

    firebaseManager->fetchAllMetadata();
}


// ───────────────────────────────
// 글로벌 단축키
// ───────────────────────────────
void MainWindow::registerGlobalHotKey()
{
#ifdef Q_OS_MAC
    qDebug("Global hotkey registration not supported on Mac via WinAPI");
    qDebug("Use Cmd+Opt+Shift+P via system shortcut settings");
#endif

#ifdef Q_OS_WIN
    if (RegisterHotKey((HWND)winId(), 1001,
                       MOD_CONTROL | MOD_SHIFT | MOD_ALT, 0x50)) {
        qDebug("Registered global hotkey Ctrl_Shift_Alt_P");
    } else {
        qDebug("Failed to register global hotkey");
    }
#endif
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray &eventType,
                             void *message, long *result)
{
    MSG *msg = static_cast<MSG *>(message);
    if (msg->message == WM_HOTKEY && msg->wParam == 1001) {
        qDebug("Global hotkey pressed: Triggering manual pull");
        performPull();
        return true;
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif


// ───────────────────────────────
// Tray 상태 업데이트
// ───────────────────────────────
void MainWindow::updateTrayStatus(const QString &status)
{
    statusAction->setText("SooNote - " + status);
    trayIcon->setToolTip("SooNote - " + status);
    qDebug("Tray status updated: %s", qPrintable(status));
}

// ───────────────────────────────
// 자동 시작 등록
// ───────────────────────────────
void MainWindow::registerAutoStart()
{
#ifdef Q_OS_MAC
    QString plistPath = QDir::homePath() +
                        "/Library/LaunchAgents/com.soonote.app.plist";

    if (QFile::exists(plistPath)) {
        qDebug("AutoStart already registered on Mac");
        return;
    }

    QString appPath = QCoreApplication::applicationFilePath();
    QString plistContent = QString(
                               "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                               "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
                               "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
                               "<plist version=\"1.0\">\n"
                               "<dict>\n"
                               "    <key>Label</key>\n"
                               "    <string>com.soonote.app</string>\n"
                               "    <key>ProgramArguments</key>\n"
                               "    <array>\n"
                               "        <string>%1</string>\n"
                               "    </array>\n"
                               "    <key>RunAtLoad</key>\n"
                               "    <true/>\n"
                               "</dict>\n"
                               "</plist>\n"
                               ).arg(appPath);

    QFile plist(plistPath);
    if (plist.open(QIODevice::WriteOnly)) {
        plist.write(plistContent.toUtf8());
        plist.close();
        qDebug("AutoStart registered on Mac: %s", qPrintable(plistPath));
    } else {
        qDebug("Failed to register AutoStart on Mac");
    }
#endif

#ifdef Q_OS_WIN
    QString runPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    QSettings settings(runPath, QSettings::NativeFormat);
    QString appPath = QCoreApplication::applicationFilePath().replace("/", "\\");
    settings.setValue("SooNote", "\"" + appPath + "\"");
    qDebug("AutoStart registered on Windows");
#endif
}

void MainWindow::unregisterAutoStart()
{
#ifdef Q_OS_MAC
    QString plistPath = QDir::homePath() +
                        "/Library/LaunchAgents/com.soonote.app.plist";
    if (QFile::remove(plistPath)) {
        qDebug("AutoStart unregistered on Mac");
    }
#endif

#ifdef Q_OS_WIN
    QString runPath = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    QSettings settings(runPath, QSettings::NativeFormat);
    settings.remove("SooNote");
    qDebug("AutoStart unregistered on Windows");
#endif
}

// ───────────────────────────────
// 폴더 접근 권한 체크
// ───────────────────────────────
bool MainWindow::checkFolderPermission(const QString &folderPath)
{
    qDebug("Checking permission for: %s", qPrintable(folderPath));

    QFileInfo info(folderPath);

    qDebug("exists: %d, readable: %d, writable: %d",
           info.exists(), info.isReadable(), info.isWritable());

    if (!info.exists()) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("SooNote 오류");
        msgBox.setText("감시 폴더가 존재하지 않습니다.\n설정에서 폴더를 다시 선택해주세요.");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();  // ← 이게 빠져있어요!
        return false;
    }

    if (!info.isReadable() || !info.isWritable()) {
        QMessageBox msgBox;
        msgBox.setWindowTitle("SooNote 권한 오류");
        msgBox.setText("폴더에 접근 권한이 없습니다.\n\n"
                       "시스템 설정 → 개인정보 보호 및 보안\n"
                       "→ 파일 및 폴더 → SooNote 허용\n\n"
                       "1 허용 후 프로그램을 다시 실행해주세요.");
        msgBox.setIcon(QMessageBox::Critical);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        return false;
    }

    qDebug("Permission check passed");
    return true;
}
void MainWindow::scanAndPushChanges()
{
    if (watchFolderPath.isEmpty()) return;

    qDebug("Scanning for offline changes...");

    QDirIterator it(watchFolderPath, QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        if (filePath.contains("/.syncnote")) continue;
        if (!isTextFile(filePath)) continue;

        QString currentHash = calculateFileHash(filePath);
        QString savedHash   = getLocalMetadata(filePath);

        if (currentHash != savedHash) {
            qDebug("Offline change detected: %s", qPrintable(filePath));
            pendingFilePath = filePath;
            onDebounceTimeout();
        }
    }

    qDebug("Offline scan completed");
}
void MainWindow::testFirebaseConnection()
{
    if (firebaseApiKey.isEmpty() || firebaseProjectId.isEmpty()) {
        return;
    }

    qDebug("Testing Firebase connection...");

    QString url = QString("https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents?key=%2")
                      .arg(firebaseProjectId)
                      .arg(firebaseApiKey);

    QNetworkRequest request(url);
    QNetworkReply *reply = firebaseManager->getNetworkManager()->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Firebase connection test passed");
            updateTrayStatus("연결 성공");
        } else {
            qDebug("Firebase connection test failed: %s", qPrintable(reply->errorString()));
            QMessageBox msgBox;
            msgBox.setWindowTitle("Firebase 연결 오류");
            msgBox.setText("Firebase 연결에 실패했습니다.\n\n"
                           "API Key 또는 Project ID를 확인해주세요.\n\n"
                           "설정 메뉴에서 다시 입력할 수 있습니다.");
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
            msgBox.exec();
        }
        reply->deleteLater();
    });
}

QString MainWindow::getLocalModifiedTime(const QString &filePath)
{
    QSqlQuery query(db);
    query.prepare("SELECT last_modified FROM file_metadata WHERE file_path = :path");
    query.bindValue(":path", filePath);
    if (query.exec() && query.next()) {
        return query.value(0).toString();
    }
    return QString();
}
