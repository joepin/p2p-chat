#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTimer>
#include <QTextEdit>
#include <QLineEdit>
#include <QHostInfo>
#include <QUdpSocket>

#define TIMEOUT 1
#define ANTI_ENTROPY_TIME 10

////////

class NetSocket : public QUdpSocket {
  Q_OBJECT

  public:
    // Initialize the net socket.
    NetSocket();

    // Bind this socket to a P2Papp-specific default port.
    bool bind();

    // Send a datagram to neighbors.
    qint64 writeDatagram(QByteArray*, int, quint16);

    // Find ports.
    void findPorts();

    // Getter for ports.
    QList<quint16> getPorts();

  private:
    quint16 myPortMin;
    quint16 myPortMax;
    quint16 myPort;
    QList<quint16> ports;
};

////////

class ChatDialog : public QDialog {
  Q_OBJECT

  // Mapping of sequence numbers to messages.
  typedef QMap<qint32, QString> Messages;

  // Mapping of origins to a map of sequence numbers and messages.
  typedef QMap<QString, Messages> Origins;

  public:
    NetSocket *sock;
    QVariantMap allMessages;
    
    ChatDialog(NetSocket*);
    
    void saveMessage(QString, qint32, QString);
    
    void sendRumorMessage(QString, qint32, QString, quint16);
    void sendStatusMessage(quint16);
    
    void handleStatusMessage(QVariantMap, quint16);
    void handleRumorMessage(QVariantMap, quint16);
    
    void prettyPrintMaps();

  public slots:
    void gotReturnPressed();
    void gotMessage();
    void timeNeighbors();

  private:
    QTextEdit *textview;
    QLineEdit *textline;
    QString myOrigin;
    qint32 mySeqNo;
    Origins originsMap;
    QVariantMap highestSeqNums;
    QList<quint16> ports;
    QList<quint16> myNeighbors;
    bool timing;

    void getPorts();
    void addNeighbors(quint16 port);
    void getNeighbors();
};

////////

#endif // P2PAPP_MAIN_HH
