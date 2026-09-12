#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tcpclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIODevice>
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

void MainWindow::on_uploadButton_clicked()
{
    // 设置按钮不可点
    ui->uploadButton->setEnabled(false);
    QFile file(_file_path);
    if(!file.open(QIODevice::ReadOnly)){
        qWarning() << "Could not open file:" << file.errorString();
        return;
    }

    QFileInfo fileInfo(_file_path); // 用完整路径构造
    int total_size = fileInfo.size();
    int last_seq = 0;
    // 计算分块大小
    if(total_size % MAX_FILE_LEN){
        last_seq = total_size  / MAX_FILE_LEN + 1;
    }else{
        last_seq = total_size / MAX_FILE_LEN;
    }

    // 读取文件内容进行发送
    QByteArray buffer;
    int seq = 0;
    while(!file.atEnd()){
        // 每次读取2048字节，分块传输
        buffer = file.read(MAX_FILE_LEN);
        QJsonObject jsonObj;
        // 将文件内容转换位Base64编码
        QString base64Data = buffer.toBase64();
        seq++;
        jsonObj["md5"] = _file_md5;
        jsonObj["name"] = fileInfo.fileName(); //提取出文件名
        jsonObj["seq"] = seq;
        jsonObj["trans_size"] = buffer.size() + (seq-1)*MAX_FILE_LEN; //已经传输的大小
        jsonObj["total_size"] = total_size;
        if(buffer.size() < MAX_FILE_LEN){
            jsonObj["last"] = 1;
        }else{
            jsonObj["last"] = 0;
        }
        jsonObj["data"] = base64Data;
        jsonObj["last_seq"] = last_seq;
        QJsonDocument doc(jsonObj);
        auto send_data = doc.toJson();
        emit TcpClient::GetInstance()->sig_send_msg(ID_UPLOAD_FILE_REQ, send_data);
    }
}

