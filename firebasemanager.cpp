#include "firebasemanager.h"
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QFile>
#include <QTimer>

FirebaseManager::FirebaseManager(QObject *parent)
    : QObject(parent)
{
    networkManager = new QNetworkAccessManager(this);
    qDebug("FirebaseManager initialized");
}

void FirebaseManager::setConfig(const QString &apiKey, const QString &projectId)
{
    this->apiKey     = apiKey;
    this->projectId  = projectId;
    qDebug("FirebaseManager config set");
}


QString FirebaseManager::storageBaseUrl() const
{
    return QString("https://firebasestorage.googleapis.com/v0/b/%1.firebasestorage.app/o")
    .arg(projectId);
}
QString FirebaseManager::firestoreBaseUrl() const
{
    return QString("https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents")
    .arg(projectId);
}

// ───────────────────────────────
// Storage - Upload
// ───────────────────────────────
void FirebaseManager::uploadFile(const QString &localPath,
                                 const QString &remotePath,
                                 const QByteArray &compressed)
{

    if (activeUploads >= maxConcurrentUploads) {
        qDebug("Upload queue full. Skipping: %s", qPrintable(remotePath));
        emit uploadFinished(remotePath, false);
        return;
    }

    activeUploads++;
    qDebug("Active uploads: %d", activeUploads);

    QString encodedName = QString(QUrl::toPercentEncoding(remotePath));
    QString url = QString("https://firebasestorage.googleapis.com/v0/b/%1.firebasestorage.app/o?uploadType=media&name=%2&key=%3")
                      .arg(projectId)
                      .arg(encodedName)
                      .arg(apiKey);



    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    QNetworkReply *reply = networkManager->post(request, compressed);
    qDebug("Encoded URL path: %s", qPrintable(encodedName));

    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath, remotePath, compressed]() {
            activeUploads--;
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Upload finished for: %s", qPrintable(remotePath));
            retryCount.remove(remotePath);
            emit uploadFinished(remotePath, true);
        } else {
            qDebug("Upload error body: %s", reply->readAll().constData());
            int count = retryCount.value(remotePath, 0) + 1;
            retryCount[remotePath] = count;

            if (count < maxRetryCount) {
                qDebug("Upload failed, retrying (%d/%d): %s", count, maxRetryCount, qPrintable(remotePath));
                QTimer::singleShot(2000 * count, this, [this, localPath, remotePath, compressed]() {
                    uploadFile(localPath, remotePath, compressed);
                });
            } else {
                qDebug("Upload failed after %d retries: %s", maxRetryCount, qPrintable(remotePath));
                retryCount.remove(remotePath);
                emit uploadFinished(remotePath, false);
            }
        }
        reply->deleteLater();
    });

    qDebug("Upload started for: %s", qPrintable(remotePath));
}

void FirebaseManager::onUploadReply(QNetworkReply *reply, const QString &remotePath)
{
    if (reply->error() == QNetworkReply::NoError) {
        qDebug("Upload successful: %s", qPrintable(remotePath));
        emit uploadFinished(remotePath, true);
    } else {
        qDebug("Upload failed: %s", qPrintable(reply->errorString()));
        emit uploadFinished(remotePath, false);
    }
    reply->deleteLater();
}

// ───────────────────────────────
// Storage - Download
// ───────────────────────────────
void FirebaseManager::downloadFile(const QString &remotePath, const QString &localPath)
{
    QString encodedPath = QUrl::toPercentEncoding(remotePath);
    QString url = QString("%1/%2?alt=media&key=%3")
                      .arg(storageBaseUrl())
                      .arg(encodedPath)
                      .arg(apiKey);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath, localPath]() {
        onDownloadReply(reply, remotePath, localPath);
    });

    qDebug("Download started for: %s", qPrintable(remotePath));
}

void FirebaseManager::onDownloadReply(QNetworkReply *reply,
                                      const QString &remotePath,
                                      const QString &localPath)
{
    if (reply->error() == QNetworkReply::NoError) {
        QByteArray compressed = reply->readAll();
        qDebug("Download successful: %s", qPrintable(remotePath));
        emit downloadFinished(remotePath, localPath, compressed, true);
    } else {
        qDebug("Download failed: %s", qPrintable(reply->errorString()));
        emit downloadFinished(remotePath, localPath, QByteArray(), false);
    }
    reply->deleteLater();
}

// ───────────────────────────────
// Storage - Delete
// ───────────────────────────────
void FirebaseManager::deleteFile(const QString &remotePath)
{
    QString encodedPath = QString(QUrl::toPercentEncoding(remotePath, QByteArray("/")));
    QString url = QString("%1/%2?key=%3")
                      .arg(storageBaseUrl())
                      .arg(encodedPath)
                      .arg(apiKey);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Delete successful: %s", qPrintable(remotePath));
            retryCount.remove(remotePath);
            emit deleteFinished(remotePath, true);
        } else {
            int count = retryCount.value(remotePath, 0) + 1;
            retryCount[remotePath] = count;

            if (count < maxRetryCount) {
                qDebug("Delete failed, retrying (%d/%d): %s", count, maxRetryCount, qPrintable(remotePath));
                QTimer::singleShot(2000 * count, this, [this, remotePath]() {
                    deleteFile(remotePath);
                });
            } else {
                qDebug("Delete failed after %d retries: %s", maxRetryCount, qPrintable(remotePath));
                retryCount.remove(remotePath);
                emit deleteFinished(remotePath, false);
            }
        }
        reply->deleteLater();
    });

    qDebug("Delete started for: %s", qPrintable(remotePath));
}

// ───────────────────────────────
// Firestore - Update Metadata
// ───────────────────────────────
void FirebaseManager::updateMetadata(const QString &filePath,
                                     const QString &hash,
                                     const QString &lastModified,
                                     qint64 fileSize)
{
    QString encodedPath = QString(QUrl::toPercentEncoding(filePath, QByteArray("/")));
    QString docId = filePath;
    docId.replace("/", "__");

    QString url = QString("https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents/soonote_files/%2?key=%3")
                      .arg(projectId)
                      .arg(docId)
                      .arg(apiKey);

    QJsonObject fields;
    fields["hash"]         = QJsonObject{{"stringValue", hash}};
    fields["lastModified"] = QJsonObject{{"stringValue", lastModified}};
    fields["fileSize"]     = QJsonObject{{"integerValue", QString::number(fileSize)}};

    QJsonObject body;
    body["fields"] = fields;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QByteArray bodyData = QJsonDocument(body).toJson();

    QNetworkReply *reply = networkManager->sendCustomRequest(request, "PATCH", QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply, filePath, hash, lastModified, fileSize]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Metadata updated successfully");
            retryCount.remove(filePath);
            emit metadataUpdated(true);
        } else {
            qDebug("Metadata error body: %s", reply->readAll().constData());
            int count = retryCount.value(filePath, 0) + 1;
            retryCount[filePath] = count;

            if (count < maxRetryCount) {
                qDebug("Metadata update failed, retrying (%d/%d): %s", count, maxRetryCount, qPrintable(filePath));
                QTimer::singleShot(2000 * count, this, [this, filePath, hash, lastModified, fileSize]() {
                    updateMetadata(filePath, hash, lastModified, fileSize);
                });
            } else {
                qDebug("Metadata update failed after %d retries: %s", maxRetryCount, qPrintable(filePath));
                retryCount.remove(filePath);
                emit metadataUpdated(false);
            }
        }
        reply->deleteLater();
    });

    qDebug("Metadata update started for: %s", qPrintable(filePath));
}

// ───────────────────────────────
// Firestore - Fetch All Metadata
// ───────────────────────────────
void FirebaseManager::fetchAllMetadata()
{
    QString url = QString("%1/soonote_files?key=%2")
    .arg(firestoreBaseUrl())
        .arg(apiKey);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->get(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            qDebug("All metadata fetched successfully");
            emit allMetadataFetched(doc.object());
        } else {
            qDebug("Fetch metadata failed: %s", qPrintable(reply->errorString()));
            emit allMetadataFetched(QJsonObject());
        }
        reply->deleteLater();
    });

    qDebug("Fetching all metadata from Firestore");
}

QNetworkAccessManager* FirebaseManager::getNetworkManager() const
{
    return networkManager;
}

void FirebaseManager::deleteMetadata(const QString &remotePath)
{
    QString docId = remotePath;
    docId.replace("/", "__");

    QString url = QString("https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents/soonote_files/%2?key=%3")
                      .arg(projectId)
                      .arg(docId)
                      .arg(apiKey);

    QNetworkRequest request(url);
    QNetworkReply *reply = networkManager->deleteResource(request);

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Metadata deleted successfully: %s", qPrintable(remotePath));
        } else {
            qDebug("Metadata delete failed: %s", qPrintable(reply->errorString()));
        }
        reply->deleteLater();
    });

    qDebug("Metadata delete started for: %s", qPrintable(remotePath));
}
