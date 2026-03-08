#ifndef FIREBASEMANAGER_H
#define FIREBASEMANAGER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class FirebaseManager : public QObject
{
    Q_OBJECT

public:
    explicit FirebaseManager(QObject *parent = nullptr);

    void setConfig(const QString &apiKey, const QString &projectId);

    // Storage
    void uploadFile(const QString &localPath, const QString &remotePath, const QByteArray &compressed);
    void downloadFile(const QString &remotePath, const QString &localPath);
    void deleteFile(const QString &remotePath);

    // Firestore
    void updateMetadata(const QString &filePath, const QString &hash,
                        const QString &lastModified, qint64 fileSize);
    void fetchAllMetadata();
    QNetworkAccessManager* getNetworkManager() const;
    void deleteMetadata(const QString &remotePath);

signals:
    void uploadFinished(const QString &remotePath, bool success);
    void downloadFinished(const QString &remotePath, const QString &localPath, bool success);
    void deleteFinished(const QString &remotePath, bool success);
    void metadataUpdated(bool success);
    void allMetadataFetched(const QJsonObject &metadata);


private slots:
    void onUploadReply(QNetworkReply *reply, const QString &remotePath);
    void onDownloadReply(QNetworkReply *reply, const QString &remotePath, const QString &localPath);

private:
    QNetworkAccessManager *networkManager;
    QString apiKey;
    QString projectId;

    QString storageBaseUrl() const;
    QString firestoreBaseUrl() const;

    int maxRetryCount = 3;
    QMap<QString, int> retryCount;

    int activeUploads = 0;
    const int maxConcurrentUploads = 3;



};

#endif // FIREBASEMANAGER_H
