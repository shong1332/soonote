#include "firebasemanager.h"
#include <QNetworkRequest>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <QFile>

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

// ───────────────────────────────
// Base URLs
// ───────────────────────────────
QString FirebaseManager::storageBaseUrl() const
{
    return QString("https://firebasestorage.googleapis.com/v0/b/%1.appspot.com/o")
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

    QString encodedPath = QString(QUrl::toPercentEncoding(remotePath, QByteArray("/")));
    QString url = QString("https://firebasestorage.googleapis.com/v0/b/%1.appspot.com/o/%2?uploadType=media&key=%3")
                      .arg(projectId)
                      .arg(encodedPath)
                      .arg(apiKey);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    QNetworkReply *reply = networkManager->put(request, compressed);

    connect(reply, &QNetworkReply::finished, this, [this, reply, remotePath]() {
        onUploadReply(reply, remotePath);
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
        emit downloadFinished(remotePath, localPath, true);
    } else {
        qDebug("Download failed: %s", qPrintable(reply->errorString()));
        emit downloadFinished(remotePath, localPath, false);
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
            emit deleteFinished(remotePath, true);
        } else {
            qDebug("Delete failed: %s", qPrintable(reply->errorString()));
            emit deleteFinished(remotePath, false);
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
    // remotePath 그대로 사용 (files/ 포함)
    QString url = QString("https://firestore.googleapis.com/v1/projects/%1/databases/(default)/documents/%2?key=%3")
                      .arg(projectId)
                      .arg(encodedPath)
                      .arg(apiKey);

    QJsonObject fields;
    fields["hash"]         = QJsonObject{{"stringValue", hash}};
    fields["lastModified"] = QJsonObject{{"stringValue", lastModified}};
    fields["fileSize"]     = QJsonObject{{"integerValue", QString::number(fileSize)}};

    QJsonObject body;
    body["fields"] = fields;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = networkManager->put(request, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply->error() == QNetworkReply::NoError) {
            qDebug("Metadata updated successfully");
            emit metadataUpdated(true);
        } else {
            qDebug("Metadata update failed: %s", qPrintable(reply->errorString()));
            emit metadataUpdated(false);
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
    QString url = QString("%1/files?key=%2")
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
