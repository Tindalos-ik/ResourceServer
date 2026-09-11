#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "tcpclient.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
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
    const QString currentPath = ui->filePathLineEdit->text();
    const QString initialDirectory = currentPath.isEmpty()
        ? QString()
        : QFileInfo(currentPath).absolutePath();

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("选择要上传的文件"),
        initialDirectory,
        tr("所有文件 (*.*)"));

    if (filePath.isEmpty()) {
        return;
    }

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

    QCryptographicHash md5(QCryptographicHash::Md5);
    constexpr qint64 chunkSize = 1024 * 1024;
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(chunkSize);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
            ui->md5ValueLabel->setText(tr("计算失败"));
            ui->uploadStatusLabel->setText(
                tr("读取文件失败：%1").arg(file.errorString()));
            return;
        }
        md5.addData(chunk);
    }

    ui->md5ValueLabel->setText(QString::fromLatin1(md5.result().toHex()));
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

