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
    void on_resumeButton_clicked();
    void slot_con_success(bool);
    void slot_show_test(QString);
    void slot_upload_progress(qint64 confirmed_offset, qint64 total_size, bool completed);
    void slot_file_sync(qint64 confirmed_offset, qint64 total_size, bool completed,
                        const QString& upload_id);
    void slot_upload_error(const QString& message);

private:
    // 先同步服务端进度，再从服务端确认的 offset 继续读取本地文件。
    void requestUploadSync(bool start_upload_after_sync);
    // 一次只发送一个分片。收到服务端确认回包后才发送下一片，断线时不会积压整文件。
    void sendNextChunk(qint64 confirmed_offset);

    Ui::MainWindow *ui;
    QString _file_path; // 文件完整路径
    QString _file_md5; // 用于断点续传校验
    QString _upload_id; // md5_文件大小_分片大小，重连后保持稳定
    qint64 _file_size = 0;
    bool _upload_active = false;
    bool _has_upload_task = false; // 已成功发起过任务，暂停后才允许点击“继续上传”

signals:
    void sig_tcp_connect(QString, QString);
};
#endif // MAINWINDOW_H
