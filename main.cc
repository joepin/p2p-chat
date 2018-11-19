
#include <unistd.h>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include "main.hh"

////////

// Construct the ChatDialog window. 
// Set the layout, the sequence number, and register necessary callbacks.
ChatDialog::ChatDialog(NetSocket *s) {
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

  // Set unique identifier
  QString hostname = QHostInfo::localHostName();
  qsrand((uint) QDateTime::currentMSecsSinceEpoch());
  myOrigin = hostname + QString::number(qrand());
  qDebug() << "myOrigin is " << myOrigin;
  setWindowTitle("P2Papp - " + myOrigin);

  // Set sequence number beginning at 1
  mySeqNo = 1;

  // Register a callback on the textline's returnPressed signal
  // so that we can send the message entered by the user.
  connect(textline, SIGNAL(returnPressed()), this, SLOT(gotReturnPressed()));
  connect(s, SIGNAL(readyRead()), this, SLOT(gotMessage()));
}

// Callback when user presses "Enter" in the textline widget.
// Propogates rumor messages. 
void ChatDialog::gotReturnPressed() {
  QString message = textline->text();

  // Add the message to the chat window. 
  textview->append(message);

  // Save the message.
  saveMessage(myOrigin, mySeqNo, message);

  // Send the rumor.
  sendRumorMessage(myOrigin, mySeqNo, message);

  // Update the sequence number.
  mySeqNo++;

  // Clear the textline to get ready for the next input message.
  textline->clear();
}

// Save input sent the chat window. 
void ChatDialog::saveMessage(QString origin, qint32 seq, QString text) {
  // Check if we store this origin already.
  if (originsMap.count(origin) > 0) {
    // Add entry to the messages mapping.
    originsMap.at(origin).insert(std::pair<qint32, QString>(seq, text));
    // prettyPrintMaps();
  } else {
    // We have not seen this origin before - create a new messages map.
    Messages messagesMap;
    messagesMap.insert(std::pair<qint32, QString>(seq, text));

    // Add the messages map to the corresponding origin
    originsMap.insert(std::pair<QString, Messages>(origin, messagesMap));
    // prettyPrintMaps();
  }
}

// Serialize and propogate a rumor message.
void ChatDialog::sendRumorMessage(QString origin, qint32 seq, QString text) {

  QByteArray buf;
  QDataStream datastream(&buf, QIODevice::ReadWrite);
  QVariantMap message;

  // Serialize the message.
  message["ChatText"] = text;
  message["Origin"] = origin;
  message["SeqNo"] = seq;

  qDebug() << "";
  qDebug() << "Sending message to peers: "
    << "<\"ChatText\",\"" << message["ChatText"].toString()
    << "\"><\"Origin\",\"" << message["Origin"].toString()
    << "\"><\"SeqNo\",\"" << message["SeqNo"].toString() << "\">";
  qDebug() << "";

  datastream << message;

  // Send message to the socket. 
  sock->writeDatagram(&buf, buf.size());
}

// Callback when receiving a message from the socket. 
void ChatDialog::gotMessage() {
  NetSocket *sock = this->sock;

  // Read each datagram.
  while (sock->hasPendingDatagrams()) {
    QByteArray datagram;
    datagram.resize(sock->pendingDatagramSize());
    QHostAddress sender;
    quint16 senderPort;
    QVariantMap message;

    sock->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

    QDataStream stream(&datagram, QIODevice::ReadOnly);

    stream >> message;

    datagram.clear();

    // Check if we have a status message or a rumor message.
    if (message["Want"].isNull()) {
      // Serialize the rumor message. 
      QString mText = message["ChatText"].toString();
      QString mOrigin = message["Origin"].toString();
      qint32 mSeqNo = message["SeqNo"].toInt();
      QString messageText = mText + " {" + mSeqNo + "@" + mOrigin + "}";

      qDebug() << "";
      qDebug() << "Received rumor: "
        << "<\"ChatText\",\"" << mText
        << "\"><\"Origin\",\"" << mOrigin
        << "\"><\"SeqNo\",\"" << mSeqNo << "\">";
      qDebug() << "";

      // Check if we already have seen this message before.
      if (originsMap.count(mOrigin) > 0 && originsMap.at(mOrigin).count(mSeqNo) > 0) {
        if (originsMap.at(mOrigin).at(mSeqNo) != mText) {
          qDebug() << originsMap.at(mOrigin).at(mSeqNo);
          qDebug() << mText;
          qDebug() << "Duplicate/corrupt message receivied. Ignoring";
        } else {
          qDebug() << "Duplicate message receivied. Ignoring";
        }
        continue;
      }

      // Save the message.
      saveMessage(mOrigin, mSeqNo, mText);

      // Send the rumor to neighbors.
      sendRumorMessage(mOrigin, mSeqNo, mText);

      // Display the message in the chat dialog window. 
      textview->append(messageText);
    // @TODO:
    // - Respond to the status message.
    } else {

    }
  }
}

// Helper function to print the originsMap and corresponding the messagesMap.
void ChatDialog::prettyPrintMaps() {
  for (std::map<QString,Messages>::iterator it=originsMap.begin(); it!=originsMap.end(); ++it) {
    qDebug() << it->first;
    for (std::map<qint32,QString>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
      qDebug() << it2->first;
      qDebug() << it2->second;
    }
  }
}

////////

// Constructor for the NetSocket class.
// Define a range of UDP ports.
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

// Bind the Netsocket to a port in a range of ports defined above.
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

// Find the 2 closest (quickest responding) neighbors.
// @TODO:
// - Add timeouts to determine which neighbors are the fastest.
// - If there are no neighbors, pick two random ports.
QList<int> NetSocket::getAllNeighboringPorts() {
  QList<int> list;
  for (int i = myPortMin; i < myPortMax; i++) {
    if (i != myPort) {
      list.append(i);
    }
  }
  return list;
}

// Propogate messages to all neighbors.
qint64 NetSocket::writeDatagram(QByteArray *buf, int bufSize) {
  qint64 bytesSent = 0;
  qint64 totalBytesSent = 0;
  QList<int> neighbors = this->getAllNeighboringPorts();
  for (int p : neighbors) {
    bytesSent = QUdpSocket::writeDatagram(*buf, QHostAddress::LocalHost, p);
    if (bytesSent < 0 || bytesSent != bufSize) {
      qDebug() << "Error sending full datagram to " << QHostAddress::LocalHost << ":" << p << ".";
    } else { 
      totalBytesSent += bytesSent;
    }
  }
  return totalBytesSent;
}

////////

int main(int argc, char **argv) {
  // Initialize Qt toolkit
  QApplication app(argc,argv);

  // Create a UDP network socket
  // NetSocket sock;
  NetSocket *sock = new NetSocket();
  if (!sock->bind())
    exit(1);

  // Create an initial chat dialog window
  // ChatDialog dialog;
  ChatDialog *dialog = new ChatDialog(sock);
  dialog->show();

  // Enter the Qt main loop; everything else is event driven
  return app.exec();
}
