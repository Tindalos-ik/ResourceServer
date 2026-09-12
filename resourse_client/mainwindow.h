#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_sendTestButton_clicked();
    void on_connectButton_clicked();
    // 打开系统文件选择器，并展示所选文件的基本信息及 MD5。
    void on_chooseFileButton_clicked();
    void on_uploadButton_clicked();
    void slot_con_success(bool);
    void slot_show_test(QString);
    void slot_upload_progress(int trans_size, int total_size);

private:
    Ui::MainWindow *ui;
    QString _file_path; // 文件完整路径
    QString _file_md5; // 用于断点续传校验

signals:
    void sig_tcp_connect(QString, QString);
};
#endif // MAINWINDOW_H
