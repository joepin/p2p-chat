#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTimer>
#include <QTextEdit>
#include <QLineEdit>
#include <QHostInfo>
#include <QUdpSocket>
#include <map>

#define TIMEOUT 1
#define ANTI_ENTROPY_TIME 10

////////

class NetSocket : public QUdpSocket {
  Q_OBJECT

  public:
    NetSocket();
    // Bind this socket to a P2Papp-specific default port.
    bool bind();
    // Send data.
    qint64 writeDatagram(QByteArray*, int);
    QList<int> getAllNeighboringPorts();

  private:
    int myPortMin;
    int myPortMax;
    int myPort;
};

////////

class ChatDialog : public QDialog {
  Q_OBJECT

  // Mapping of sequence numbers to messages.
  typedef std::map<qint32, QString> Messages;
  // Mapping of origins to a map of sequence numbers and messages.
  typedef std::map<QString, Messages> Origins;

  public:
    NetSocket *sock;
    QVariantMap allMessages;
    ChatDialog(NetSocket*);
    void saveMessage(QString, qint32, QString);
    void sendRumorMessage(QString, qint32, QString);

  public slots:
    void gotReturnPressed();
    void gotMessage();
    void prettyPrintMaps();

  private:
    QTextEdit *textview;
    QLineEdit *textline;
    QString myOrigin;
    qint32 mySeqNo;
    Origins originsMap;
};

////////

#endif // P2PAPP_MAIN_HH
