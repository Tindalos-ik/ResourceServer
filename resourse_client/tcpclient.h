#ifndef TCPCLIENT_H
#define TCPCLIENT_H
#include <QObject>
#include <QByteArray>
#include <QTcpSocket>
#include "singleton.h"
#include "global.h"
#include <functional>
#include <QMap>
#include <QDataStream>

// tcp客户端，负责发送和解析数据

class TcpClient : public QObject, public Singleton<TcpClient>
{
    Q_OBJECT
    friend class Singleton<TcpClient>;
public:
    ~TcpClient();
    void sendMsg(quint16 id,QByteArray data);
private:
    TcpClient();

    void initHandlers();
    QMap<ReqId, std::function<void(ReqId id, int len, QByteArray data)>> _handler;

    QTcpSocket* _socket;
    QByteArray _buffer;
    bool _b_recy_pending;
    quint16 _message_id;
    quint32 _message_len;

    QString _host;
    uint16_t _port;

public slots:
    void slot_send_msg(quint16 id, QByteArray body);
    void slot_tcp_connect(QString, QString);

signals:
    void sig_net_error(QString);
    void sig_send_msg(quint16, QByteArray);
    void sig_show_test(QString);
    void sig_con_success(bool);
};

#endif // TCPCLIENT_H
