#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTimer>
#include <QTextEdit>
#include <QLineEdit>
#include <QHostInfo>
#include <QUdpSocket>
#include <vector>

#define TIMEOUT 1000
#define ANTI_ENTROPY_TIME 10000

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

    // Get neighbors.
    void getNeighbors();
    void addNeighbors(int port);

    // (Used by ChatDialog.)
    QList<int> myNeighbors;

  public slots:
    void timeNeighbors();

  private:
    bool timing;
    int myPortMin;
    int myPortMax;
    int myPort;
    QList<int> ports;

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

    ChatDialog(NetSocket*);

    void saveMessage(QString, QString);
    void sendRumorMessage(QString, qint32, QString, int);
    // void sendStatusMessage();
    // void sendStatusMessageToRandom();
    void prettyPrintMaps();
    void flipCoin();

  public slots:
    void gotReturnPressed();
    void gotMessage();

  private:
    QTextEdit *textview;
    QLineEdit *textline;
    QString myOrigin;
    Origins originsMap;

};

////////

#endif // P2PAPP_MAIN_HH
