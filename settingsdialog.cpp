#include "settingsdialog.h"
#include "ui_settingsdialog.h"
#include <QFileDialog>
#include <QDebug>
#include <QMessageBox>
#include <QDir>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    connect(ui->browseButton, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseClicked);

    // OK 버튼 직접 연결 (기본 accept 대신)
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onOkClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &SettingsDialog::reject);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::onBrowseClicked()
{
    QString folder = QFileDialog::getExistingDirectory(
        this,
        "감시 폴더 선택",
        QDir::homePath()
        );

    if (!folder.isEmpty()) {
        ui->watchFolderEdit->setText(folder);
        qDebug("Watch folder selected: %s", qPrintable(folder));
    }
}

QString SettingsDialog::getWatchFolder() const
{
    return ui->watchFolderEdit->text();
}

QString SettingsDialog::getApiKey() const
{
    return ui->apiKeyEdit->text();
}

QString SettingsDialog::getProjectId() const
{
    return ui->projectIdEdit->text();
}

void SettingsDialog::setCurrentSettings(const QString &watchFolder,
                                        const QString &apiKey,
                                        const QString &projectId)
{
    ui->watchFolderEdit->setText(watchFolder);
    ui->apiKeyEdit->setText(apiKey);
    ui->projectIdEdit->setText(projectId);
}

void SettingsDialog::onOkClicked()
{

    QString folder  = ui->watchFolderEdit->text();
    QString apiKey  = ui->apiKeyEdit->text();
    QString project = ui->projectIdEdit->text();

    qDebug("folder: %s", qPrintable(folder));
    qDebug("apiKey: %s", qPrintable(apiKey));
    qDebug("project: %s", qPrintable(project));

    // 빈 값 체크
    if (folder.isEmpty() || apiKey.isEmpty() || project.isEmpty()) {
        qDebug("Empty field detected");

        QMessageBox msgBox;
        msgBox.setWindowTitle("입력 오류");
        msgBox.setText("모든 항목을 입력해주세요.");
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        return;
    }

    // 폴더 존재 여부 체크
    if (!QDir(folder).exists()) {
             qDebug("Folder not exists");
        QMessageBox msgBox;
        msgBox.setWindowTitle("폴더 오류");
        msgBox.setText("선택한 폴더가 존재하지 않습니다.\n다시 확인해주세요.\n\n" + folder);
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
        msgBox.exec();
        return;
    }

    // 최종 확인
        qDebug("Showing confirmation dialog");
    QMessageBox msgBox;
    msgBox.setWindowTitle("설정 확인");
    msgBox.setText(
        "아래 설정으로 저장합니다.\n\n"
        "감시 폴더: " + folder + "\n"
                   "Project ID: " + project + "\n\n"
                    "맞습니까?"
        );
    msgBox.setIcon(QMessageBox::Question);

    QPushButton *yesBtn = msgBox.addButton("확인", QMessageBox::YesRole);
    QPushButton *noBtn  = msgBox.addButton("취소", QMessageBox::NoRole);
    Q_UNUSED(noBtn);

    msgBox.setWindowFlags(Qt::WindowStaysOnTopHint);
    msgBox.exec();

    if (msgBox.clickedButton() == yesBtn) {
        qDebug("Calling accept()");
        accept();
    }
}
