#include "tcpclient.h"
#include <QJsonDocument>
#include <QJsonObject>

TcpClient::~TcpClient()
{

}

TcpClient::TcpClient(): _b_recy_pending(false), _message_id(0), _message_len(0){

    _socket = new QTcpSocket();
    //_socket连接服务器成功之后，发送信号通知一下
    connect(_socket, &QTcpSocket::connected, [this](){
        qDebug() << "connect to server" << Qt::endl;
        emit sig_con_success(true);
    });

    // 记录断开原因，便于区分客户端主动退出、服务端关闭和网络错误。
    connect(_socket, &QTcpSocket::disconnected, [this]{
        qWarning() << "disconnected from server:" << _socket->errorString();
        _buffer.clear();
        _b_recy_pending = false;
    });

    //在有数据可读时候进行处理
    connect(_socket,&QTcpSocket::readyRead,[this](){
        //读取所有数据到缓冲区
        _buffer.append(_socket->readAll());

        // 一次 readyRead 可能包含多个完整包；半包则保留已读包头，等下次数据补齐包体。
        while (true) {
            //解析头部，消息头是消息id + 消息长度 2 + 4
            if(!_b_recy_pending){
                //检查缓冲区中的数据是否足够解析出一个消息头，不够就返回
                //使用static_cast<> 实现更加安全的类型转换
                if(_buffer.size() < static_cast<int>(sizeof(quint16)*3)){
                    return;
                }

                QDataStream stream(&_buffer, QIODevice::ReadOnly);
                stream.setVersion(QDataStream::Qt_6_0);
                stream.setByteOrder(QDataStream::BigEndian);

                //预读取消息id和消息长度
                stream >> _message_id >> _message_len;

                //将buffer中前四个字节移除，mid截取一段
                _buffer = _buffer.mid(sizeof(quint16)*3);

                //输出读取的数据
                qDebug() << "message id : " << _message_id
                         << "message len : " << _message_len << Qt::endl;
                _b_recy_pending = true;
            }

            //buffer剩余长度是否满足消息体长度，不满足就退出继续等待接受
            if(_buffer.size() < _message_len){
                return;
            }


            //读取消息体，给到回调函数处理
            QByteArray messageBody = _buffer.mid(0,_message_len);
            qDebug() << "receive message : " << messageBody << Qt::endl;
            _buffer = _buffer.mid(_message_len);
            _b_recy_pending = false;

            //处理收到的数据
            auto iter = _handler.find(ReqId(_message_id));
            if(iter == _handler.end()){
                qDebug() << "id error" << Qt::endl;
                continue;
            }

            //执行处理函数
            iter.value()(ReqId(_message_id), _message_len, messageBody);
        }

    });

    //处理错误，直接问ai
    connect(_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            [this](QAbstractSocket::SocketError socketError){
                Q_UNUSED(socketError);
                qDebug() << "Error : " << _socket->errorString();
            });

    //连接 发送数据信号和槽函数
    connect(this, &TcpClient::sig_send_msg, this, &TcpClient::slot_send_msg);

    initHandlers();

}

void TcpClient::initHandlers()
{
    _handler[ID_TEST_MSG_RSP] = [this](ReqId id, int len, QByteArray data){
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << "data is " << data;
        QString test_str = QString::fromUtf8(data);
        emit sig_show_test(test_str);
    };

    //文件上传回包，服务端写入文件成功后才会回包
    _handler.insert(ReqId::ID_UPLOAD_FILE_RSP, [this](ReqId id, int len, QByteArray data) {
        Q_UNUSED(len);
        qDebug() << "handle id is " << id << "data is " << data;

        //将字节流转换为json文档
        QJsonDocument jsonDoc = QJsonDocument::fromJson(data);

        //检查转换是否成功
        if(jsonDoc.isNull()){
            qDebug() << "failed to create QJsonDocument";
            return;
        }

        //将json文档转换为json对象
        QJsonObject json_obj = jsonDoc.object();

        //正常回包必须有error字段，用来判断服务端是否成功写入该分片
        if(!json_obj.contains("error")){
            qDebug() << "Upload Failed, err is Json Parse Err";
            return;
        }

        int err = json_obj["error"].toInt();
        if(err != ErrorCodes::Success){
            qDebug() << "Upload Failed, err is" << err;
            return;
        }

        //trans_size是服务端已经保存的字节数，不能用客户端发送量代替
        int trans_size = json_obj["trans_size"].toInt();
        int total_size = json_obj["total_size"].toInt();
        emit sig_upload_progress(trans_size, total_size);
    });

}



//发送数据槽函数
void TcpClient::slot_send_msg(quint16 id, QByteArray body)
{
    //如果连接异常则直接返回
    if(_socket->state() != QAbstractSocket::ConnectedState){
        emit sig_net_error(QString("断开连接无法发送"));
        return;
    }

    //获取body的长度
    quint32 bodyLength = body.size();

    //创建字节数组
    QByteArray data;
    //绑定字节数组
    QDataStream stream(&data, QIODevice::WriteOnly);
    //设置大端模式
    stream.setByteOrder(QDataStream::BigEndian);
    //写入ID
    stream << id;
    //写入长度
    stream << bodyLength;
    //写入包体
    data.append(body);

    //发送消息
    _socket->write(data);
}

void TcpClient::slot_tcp_connect(QString Host, QString Port)
{
    //客户端连接服务器
    qDebug() << "connecting to server..." << Qt::endl;
    _host = Host;
    _port = static_cast<uint16_t>(Port.toUInt()); //QString很好用
    _socket->connectToHost(_host, _port); //通过前面的回调函数知道结果
}


void TcpClient::sendMsg(quint16 id,QByteArray data)
{
    //发送信号，统一交给槽函数处理，这么做的好处是多线程安全
    emit sig_send_msg(id, data);
}

