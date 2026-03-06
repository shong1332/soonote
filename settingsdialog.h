#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog();

    QString getWatchFolder() const;
    QString getApiKey() const;
    QString getProjectId() const;
    void setCurrentSettings(const QString &watchFolder,
                            const QString &apiKey,
                            const QString &projectId);

private slots:
    void onBrowseClicked();
    void onOkClicked();

private:
    Ui::SettingsDialog *ui;



};

#endif // SETTINGSDIALOG_H
