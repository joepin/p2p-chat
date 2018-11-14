
#include <unistd.h>

#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>

#include "main.hh"

ChatDialog::ChatDialog(NetSocket *s) {
	setWindowTitle("P2Papp");
	// Read-only text box where we display messages from everyone.
	// This widget expands both horizontally and vertically.
	textview = new QTextEdit(this);
	textview->setReadOnly(true);

	// Small text-entry box the user can enter messages.
	// This widget normally expands only horizontally,
	// leaving extra vertical space for the textview widget.
	//
	// You might change this into a read/write QTextEdit,
	// so that the user can easily enter multi-line messages.
	textline = new QLineEdit(this);

	// Lay out the widgets to appear in the main window.
	// For Qt widget and layout concepts see:
	// http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
	QVBoxLayout *layout = new QVBoxLayout();
	layout->addWidget(textview);
	layout->addWidget(textline);
	setLayout(layout);

  sock = s;

  // Register a callback on the textline's returnPressed signal
  // so that we can send the message entered by the user.
  connect(textline, SIGNAL(returnPressed()), this, SLOT(gotReturnPressed()));
  connect(s, SIGNAL(readyRead()), this, SLOT(gotMessage()));
}

void ChatDialog::gotReturnPressed() {

  QByteArray buf;
  QDataStream s(&buf, QIODevice::ReadWrite);
  QMap<QString, QVariant> message;

  message["ChatText"] = textline->text();
  s << message;
	// Initially, just echo the string locally.
	// Insert some networking code here...
	qDebug() << "FIX: send message to other peers: " << textline->text();
	textview->append(textline->text());

  sock->writeDatagram(&buf);

	// Clear the textline to get ready for the next input message.
	textline->clear();
}

void ChatDialog::gotMessage() {
  QMap<QString, QVariant> message;
  NetSocket *sock = this->sock;

  while (sock->hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(sock->pendingDatagramSize());
    QHostAddress sender;
    quint16 senderPort;

    sock->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

    QDataStream stream(&datagram, QIODevice::ReadOnly);

    stream >> message;

    datagram.clear();
  }

  QString messageText = message["ChatText"].toString();
  qDebug() << messageText;

  textview->append(messageText);

}

NetSocket::NetSocket() {
	// Pick a range of four UDP ports to try to allocate by default,
	// computed based on my Unix user ID.
	// This makes it trivial for up to four P2Papp instances per user
	// to find each other on the same host,
	// barring UDP port conflicts with other applications
	// (which are quite possible).
	// We use the range from 32768 to 49151 for this purpose.
	myPortMin = 32768 + (getuid() % 4096)*4;
	myPortMax = myPortMin + 3;
  qDebug() << myPortMin << " - " << myPortMax;
}

bool NetSocket::bind() {
	// Try to bind to each of the range myPortMin..myPortMax in turn.
	for (int p = myPortMin; p <= myPortMax; p++) {
		if (QUdpSocket::bind(p)) {
			qDebug() << "bound to UDP port " << p;
      myPort = p;
			return true;
		}
	}

	qDebug() << "Oops, no ports in my default range " << myPortMin
		<< "-" << myPortMax << " available";
	return false;
}

QList<int> NetSocket::getAllNeighboringPorts() {
  QList<int> list;
  for (int i = myPortMin; i < myPortMax; i++) {
    if (i != myPort) {
      list.append(i);
    }
  }
  return list;
}

qint64 NetSocket::writeDatagram(QByteArray *buf) {
  QList<int> neighbors = this->getAllNeighboringPorts();
  qint64 bytesSent;
  for (int p : neighbors) {
    if ((bytesSent = QUdpSocket::writeDatagram(*buf, QHostAddress::LocalHost, p)) < 0) {
      return bytesSent;
    }
    bytesSent = 0;
  }
  return bytesSent;
}

int main(int argc, char **argv) {
	// Initialize Qt toolkit
	QApplication app(argc,argv);

  // Create a UDP network socket
  NetSocket *sock = new NetSocket();
  if (!sock->bind())
    exit(1);

	// Create an initial chat dialog window
	ChatDialog *dialog = new ChatDialog(sock);
	dialog->show();

	// Enter the Qt main loop; everything else is event driven
	return app.exec();
}

