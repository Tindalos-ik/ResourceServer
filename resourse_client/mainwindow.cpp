#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tcpclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIODevice>
#include <QMessageBox>
#include "global.h"
#include "tcpclient.h"
#include <QJsonObject>
#include <QJsonDocument>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , _file_path("")
    , _file_md5("")
{
    ui->setupUi(this);

    connect(this, &MainWindow::sig_tcp_connect, TcpClient::GetInstance().get(), &TcpClient::slot_tcp_connect);
    connect(TcpClient::GetInstance().get(), &TcpClient::sig_show_test, this, &MainWindow::slot_show_test);
    connect(TcpClient::GetInstance().get(), &TcpClient::sig_con_success, this, &MainWindow::slot_con_success);
    //接到服务端写文件成功的回包后，更新上传进度条
    connect(TcpClient::GetInstance().get(), &TcpClient::sig_upload_progress,
            this, &MainWindow::slot_upload_progress);
    connect(TcpClient::GetInstance().get(), &TcpClient::sig_file_sync,
            this, &MainWindow::slot_file_sync);
    connect(TcpClient::GetInstance().get(), &TcpClient::sig_upload_error,
            this, &MainWindow::slot_upload_error);

    // 未开始上传前没有可继续的任务，避免“继续上传”意外创建一个新任务。
    ui->resumeButton->setEnabled(false);
    ui->resumeButton->setText(tr("暂停上传"));

}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_sendTestButton_clicked()
{
    QString test_str = ui->messagePlainTextEdit->toPlainText();
    QByteArray test_data = test_str.toUtf8();

    emit TcpClient::GetInstance()->sig_send_msg(ID_TEST_MSG_REQ, test_data);
}


void MainWindow::on_connectButton_clicked()
{
    QString host = ui->hostLineEdit->text();
    QString port = ui->portSpinBox->text();
    emit sig_tcp_connect(host, port);
}

void MainWindow::on_chooseFileButton_clicked()
{
    // 再次选择文件时从上次文件所在目录打开，减少重复查找路径的操作。
    const QString currentPath = ui->filePathLineEdit->text();
    const QString initialDirectory = currentPath.isEmpty()
        ? QString()
        : QFileInfo(currentPath).absolutePath();

    // 使用 Qt 原生文件对话框；当前不限制文件类型，任意文件均可上传。
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择要上传的文件"),
        initialDirectory,
        tr("所有文件 (*.*)"));
    _file_path = filePath;

    // 用户取消选择时保留界面中原有的文件信息。
    if (filePath.isEmpty()) {
        return;
    }

    // 切换文件后，旧文件对应的暂停任务不能继续使用。
    _upload_active = false;
    _has_upload_task = false;
    ui->resumeButton->setEnabled(false);
    ui->resumeButton->setText(tr("暂停上传"));

    // 先更新无需读取文件内容即可获得的路径、文件名和精确字节数。
    const QFileInfo fileInfo(filePath);
    ui->filePathLineEdit->setText(fileInfo.absoluteFilePath());
    ui->fileNameValueLabel->setText(fileInfo.fileName());
    ui->fileSizeValueLabel->setText(
        tr("%1 字节").arg(fileInfo.size()));

    QFile file(fileInfo.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        ui->md5ValueLabel->setText(tr("计算失败"));
        ui->uploadStatusLabel->setText(
            tr("无法读取文件：%1").arg(file.errorString()));
        return;
    }

    // 按 1 MB 分块计算 MD5，避免大文件被一次性读入内存。
    QCryptographicHash md5(QCryptographicHash::Md5);
    constexpr qint64 chunkSize = 1024 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(chunkSize);

        // 空数据可能代表读取失败；正常到达文件末尾由 atEnd() 处理。
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            ui->md5ValueLabel->setText(tr("计算失败"));
            ui->uploadStatusLabel->setText(
                tr("读取文件失败：%1").arg(file.errorString()));
            return;
        }
        md5.addData(chunk);
    }

    // MD5 以常见的 32 位小写十六进制字符串显示。
    _file_md5 = QString::fromLatin1(md5.result().toHex());
    ui->md5ValueLabel->setText(_file_md5);
    ui->uploadStatusLabel->setText(
        tr("已选择：%1").arg(fileInfo.fileName()));
}

void MainWindow::slot_con_success(bool)
{
    ui->connectionStatusLabel->setText("已连接");
}

void MainWindow::slot_show_test(QString test)
{
    ui->responsePlainTextEdit->setPlainText(test);
}

void MainWindow::slot_upload_progress(qint64 confirmed_offset, qint64 total_size, bool completed)
{
    // 范围和进度都以服务端确认的值为准；本地发送成功不等于服务端已经落盘。
    // QProgressBar 的 range 是 int；使用千分比可显示超过 2GB 的 qint64 文件。
    ui->uploadProgressBar->setRange(0, 1000);
    ui->uploadProgressBar->setValue(total_size == 0 ? 1000
        : static_cast<int>(confirmed_offset * 1000 / total_size));

    ui->uploadStatusLabel->setText(
        tr("已上传 %1 / %2 字节").arg(confirmed_offset).arg(total_size));

    if (completed) {
        ui->uploadStatusLabel->setText(tr("上传完成：%1 字节").arg(total_size));
        ui->uploadButton->setEnabled(true);
        _upload_active = false;
        ui->resumeButton->setEnabled(false);
        ui->resumeButton->setText(tr("继续上传"));
        return;
    }

    // 每收到一个确认包再发送下一分片，避免断线时把完整文件堆积在 socket 写队列中。
    if (_upload_active) {
        sendNextChunk(confirmed_offset);
    }
}

void MainWindow::on_uploadButton_clicked()
{
    // 上传依赖 TCP 长连接；未连接时不读取文件、不禁用按钮，也不发送分片。
    if (!TcpClient::GetInstance()->IsConnected()) {
        ui->uploadStatusLabel->setText(tr("未连接服务器，请先连接后再上传。"));
        QMessageBox::warning(this, tr("未连接服务器"),
                             tr("请先连接服务器，然后再开始上传文件。"));
        return;
    }

    if (_file_path.isEmpty() || _file_md5.isEmpty()) {
        ui->uploadStatusLabel->setText(tr("请先选择一个可读取的文件。"));
        return;
    }

    const QFileInfo fileInfo(_file_path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        ui->uploadStatusLabel->setText(tr("所选文件已不存在，或不是普通文件。"));
        return;
    }

    _file_size = fileInfo.size();
    // upload_id 不包含 session_id，所以重新连接后仍指向同一个上传任务目录。
    _upload_id = QString("%1_%2_%3").arg(_file_md5).arg(_file_size).arg(MAX_FILE_LEN);

    // 设置按钮不可点
    ui->uploadButton->setEnabled(false);
    //开始新的上传任务，先将进度条清零，收到回包后再根据文件大小设置范围
    ui->uploadProgressBar->setRange(0, 100);
    ui->uploadProgressBar->setValue(0);
    ui->uploadStatusLabel->setText(tr("正在上传..."));
    _upload_active = true;
    _has_upload_task = true;
    ui->resumeButton->setEnabled(true);
    ui->resumeButton->setText(tr("暂停上传"));
    requestUploadSync(true);
}

void MainWindow::on_resumeButton_clicked()
{
    // 暂停不取消已经写入服务器的内容；若正有一个分片在途，收到其确认后不会再发下一片。
    if (_upload_active) {
        _upload_active = false;
        ui->resumeButton->setText(tr("继续上传"));
        ui->uploadStatusLabel->setText(tr("上传已暂停，将保留服务端已确认的进度。"));
        return;
    }

    if (!_has_upload_task) {
        return;
    }
    if (!TcpClient::GetInstance()->IsConnected()) {
        ui->uploadStatusLabel->setText(tr("未连接服务器，无法继续上传。"));
        return;
    }
    if (_file_path.isEmpty() || _file_md5.isEmpty()) {
        ui->uploadStatusLabel->setText(tr("请先选择一个文件。"));
        return;
    }

    const QFileInfo fileInfo(_file_path);
    if (!fileInfo.exists() || !fileInfo.isFile()) {
        ui->uploadStatusLabel->setText(tr("所选文件已不存在，或不是普通文件。"));
        return;
    }
    if (fileInfo.size() != _file_size) {
        slot_upload_error(tr("本地文件大小已改变，请重新选择文件后上传。"));
        return;
    }

    // 继续前重新同步 offset：暂停期间即使服务端已确认在途分片，也能从正确位置开始。
    _upload_active = true;
    ui->uploadButton->setEnabled(false);
    ui->resumeButton->setText(tr("暂停上传"));
    requestUploadSync(true);
}

void MainWindow::requestUploadSync(bool start_upload_after_sync)
{
    QJsonObject request;
    request["upload_id"] = _upload_id;
    request["md5"] = _file_md5;
    request["name"] = QFileInfo(_file_path).fileName();
    request["total_size"] = QJsonValue::fromVariant(_file_size);
    request["chunk_size"] = MAX_FILE_LEN;
    request["start_upload"] = start_upload_after_sync;

    ui->uploadStatusLabel->setText(start_upload_after_sync
        ? tr("正在同步服务器续传进度...")
        : tr("正在查询服务器续传进度..."));
    emit TcpClient::GetInstance()->sig_send_msg(ID_SYNC_FILE_REQ,
                                                QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MainWindow::slot_file_sync(qint64 confirmed_offset, qint64 total_size, bool completed,
                                const QString& upload_id)
{
    if (upload_id != _upload_id || total_size != _file_size) {
        slot_upload_error(tr("服务端返回的上传任务与当前选择的文件不一致。"));
        return;
    }

    ui->uploadProgressBar->setRange(0, 1000);
    ui->uploadProgressBar->setValue(total_size == 0 ? 1000
        : static_cast<int>(confirmed_offset * 1000 / total_size));
    if (completed) {
        ui->uploadStatusLabel->setText(tr("服务器已有完整文件，已秒传完成。"));
        ui->uploadButton->setEnabled(true);
        _upload_active = false;
        ui->resumeButton->setEnabled(false);
        ui->resumeButton->setText(tr("继续上传"));
        return;
    }

    ui->uploadStatusLabel->setText(
        tr("服务端已确认 %1 / %2 字节。").arg(confirmed_offset).arg(total_size));
    if (_upload_active) {
        sendNextChunk(confirmed_offset);
    }
}

void MainWindow::sendNextChunk(qint64 confirmed_offset)
{
    if (confirmed_offset < 0 || confirmed_offset > _file_size) {
        slot_upload_error(tr("服务端返回了非法的续传偏移量。"));
        return;
    }

    QFile file(_file_path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(confirmed_offset)) {
        slot_upload_error(tr("无法定位本地文件：%1").arg(file.errorString()));
        return;
    }

    // 空文件也发送一个空的最后分片，使服务端可以发布它为完整文件。
    const QByteArray buffer = file.read(MAX_FILE_LEN);
    if (buffer.isEmpty() && confirmed_offset < _file_size) {
        slot_upload_error(tr("读取本地文件失败：%1").arg(file.errorString()));
        return;
    }

    const qint64 next_offset = confirmed_offset + buffer.size();
    QJsonObject request;
    request["upload_id"] = _upload_id;
    request["md5"] = _file_md5;
    request["name"] = QFileInfo(_file_path).fileName();
    request["total_size"] = QJsonValue::fromVariant(_file_size);
    request["offset"] = QJsonValue::fromVariant(confirmed_offset);
    request["is_last"] = (next_offset == _file_size);
    request["data"] = QString::fromLatin1(buffer.toBase64());
    emit TcpClient::GetInstance()->sig_send_msg(ID_UPLOAD_FILE_REQ,
                                                QJsonDocument(request).toJson(QJsonDocument::Compact));
}

void MainWindow::slot_upload_error(const QString& message)
{
    _upload_active = false;
    ui->uploadButton->setEnabled(true);
    ui->resumeButton->setText(tr("继续上传"));
    ui->uploadStatusLabel->setText(message);
}
