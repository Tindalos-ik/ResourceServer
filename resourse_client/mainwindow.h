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
    void on_chooseFileButton_clicked();
    void slot_con_success(bool);
    void slot_show_test(QString);

private:
    Ui::MainWindow *ui;

signals:
    void sig_tcp_connect(QString, QString);
};
#endif // MAINWINDOW_H
