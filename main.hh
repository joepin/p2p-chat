#ifndef P2PAPP_MAIN_HH
#define P2PAPP_MAIN_HH

#include <QDialog>
#include <QTextEdit>
#include <QLineEdit>
#include <QUdpSocket>

class NetSocket : public QUdpSocket
{
  Q_OBJECT

public:
  NetSocket();

  // Bind this socket to a P2Papp-specific default port.
  bool bind();
  // send data
  qint64 writeDatagram(QByteArray*);
  QList<int> getAllNeighboringPorts();

private:
  int myPortMin;
  int myPortMax;
  int myPort;
};


class ChatDialog : public QDialog {
	Q_OBJECT

public:
  NetSocket *sock;
  QVariantMap allMessages;
  ChatDialog(NetSocket*);
  void sendRumorMessage(QString);

public slots:
	void gotReturnPressed();
  void gotMessage();

private:
	QTextEdit *textview;
	QLineEdit *textline;
  QString myOrigin;
  qint32 mySeqNo;
};

#endif // P2PAPP_MAIN_HH
