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
  qint64 writeDatagram(QByteArray *buf);
  QList<int> getAllNeighboringPorts();

private:
  int myPortMin;
  int myPortMax;
  int myPort;
};


class ChatDialog : public QDialog {
	Q_OBJECT

public:
	ChatDialog(NetSocket *s);
  NetSocket *sock;

public slots:
	void gotReturnPressed();
  void gotMessage();

private:
	QTextEdit *textview;
	QLineEdit *textline;
  QString origin;
};

#endif // P2PAPP_MAIN_HH
