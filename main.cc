
#include <unistd.h>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QPair>
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
  qsrand((uint) QDateTime::currentMSecsSinceEpoch());
  myOrigin = QString::number(qrand());
  qDebug() << "myOrigin is " << myOrigin;
  setWindowTitle("P2Papp - " + myOrigin);

  // Set sequence number beginning at 1
  mySeqNo = 1;

  // initialize a map to save the highest sequence numbers seen so far
  highestSeqNums = QVariantMap();
  highestSeqNums[myOrigin] = mySeqNo;

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
  qDebug() << "highest seq num for origin " + origin + ": " + QString::number(highestSeqNums[origin].toInt());
  // check if we have this origin already
  if (!originsMap[origin].isEmpty()) {
    // check if the sequence number is the next sequentially
    if (seq == highestSeqNums[origin].toInt() + 1) {
      // it is - we're good to save it
      originsMap[origin][seq] = text;
      highestSeqNums[origin] = seq;
    }
    // prettyPrintMaps();
  } else {
    // We have not seen this origin before - create a new messages map.
    Messages messagesMap;
    messagesMap[seq] = text;

    // Add the messages map to the corresponding origin
    originsMap[origin] = messagesMap;
    highestSeqNums[origin] = seq;
    // prettyPrintMaps();
  }
  qDebug() << "highest seq num for origin " + origin + ": " + QString::number(highestSeqNums[origin].toInt());
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

// Serialize and propogate a status message.
void ChatDialog::sendStatusMessage(quint16 senderPort) {
  QByteArray datagram;
  QDataStream datastream(&datagram, QIODevice::ReadWrite);
  QVariantMap message = QVariantMap();
  QVariantMap originsToSeq = QVariantMap();

  for (auto o : highestSeqNums.keys()) {
    originsToSeq[o] = highestSeqNums[o];
  }

  message["Want"] = originsToSeq;

  datastream << message;
  qDebug() << "Sending status message to " << senderPort;
  qDebug() << "Status message to be sent is " << message;
  // Send message to the socket. 
  sock->writeDatagram(senderPort, &datagram, datagram.size());
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


    qDebug() << "Message is " << message;

    // Check if we have a status message
    if (!message["Want"].isNull()) {
      // we have a status message
      handleStatusMessage(message, senderPort);
    } else if (!message["ChatText"].isNull()) {
      // we have a rumor message
      handleRumorMessage(message, senderPort);
    } else {
      // corrupt or other message
    }
      
    datagram.clear();
    // @TODO:
    // - Respond to the status message.
  }
}

void ChatDialog::handleStatusMessage(QVariantMap m, quint16 senderPort) {
  qDebug() << "we have a status message: " << m;
  QMap<QString, QVariant> wantMap = m.value("Want").toMap();
  qDebug() << "wantMap is " << wantMap;
  for (auto mOrigin : wantMap.keys()) {
    quint32 mSeqNo = wantMap.value(mOrigin).toInt();
    qDebug() << "mOrigin: " << mOrigin << " value: " << mSeqNo;
    if (highestSeqNums[mOrigin].toInt() == mSeqNo) {
      // we have the same latest message as the status message, we can ignore
      qDebug() << "equal";
      continue;
    }  else if (highestSeqNums[mOrigin].toInt() < mSeqNo) {
      qDebug() << "less";
      // we need the 
    } else {
      qDebug() << "greater";
    }
  }
}

void ChatDialog::handleRumorMessage(QVariantMap m, quint16 senderPort) {
  // serialize the rumor message
    QString mText = m["ChatText"].toString();
    QString mOrigin = m["Origin"].toString();
    qint32 mSeqNo = m["SeqNo"].toInt();
    QString messageText = mText + " {" + QString::number(mSeqNo) + "@" + mOrigin + "}";
    qDebug() << "messageText: " << messageText;

    qDebug() << "";
    qDebug() << "Received rumor: "
      << "<\"ChatText\",\"" << mText
      << "\"><\"Origin\",\"" << mOrigin
      << "\"><\"SeqNo\",\"" << mSeqNo << "\">";
    qDebug() << "";

    // check that this sequence number is in the correct order
    if (mSeqNo == highestSeqNums[mOrigin].toInt() + 1) {
      // it's in order, now let's check we don't already have it
      if (originsMap[mOrigin][mSeqNo].isNull()) {
        // we don't have it; save it
        saveMessage(mOrigin, mSeqNo, mText);
        // propogate the rumor to neighbors
        sendRumorMessage(mOrigin, mSeqNo, mText);
      }
    }
    // always send a status message
    sendStatusMessage(senderPort);
    // Display the message in the chat dialog window. 
    textview->append(messageText);
}


// Helper function to print the originsMap and corresponding the messagesMap.
void ChatDialog::prettyPrintMaps() {
  for (auto origin : originsMap.keys()) {
    qDebug() << origin;
    Messages messages = originsMap.value(origin);
    for (auto message : messages.keys()) {
      qDebug() << message << ", " << messages.value(message);
    }
  }
}

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
  qint64 totalBytesSent = 0;
  QList<int> neighbors = this->getAllNeighboringPorts();
  qDebug() << "Sending to these neighbors: " << neighbors;
  for (int p : neighbors) {
    totalBytesSent += writeDatagram(p, buf, bufSize);
  }
  return totalBytesSent;
}

// Send messages to one specific neighbor, by port number.
qint64 NetSocket::writeDatagram(quint16 senderPort, QByteArray *buf, int bufSize) {
  qint64 bytesSent = 0;
  qint64 totalBytesSent = 0;
  bytesSent = QUdpSocket::writeDatagram(*buf, QHostAddress::LocalHost, senderPort);
  if (bytesSent < 0 || bytesSent != bufSize) {
    qDebug() << "Error sending full datagram to " << QHostAddress::LocalHost << ":" << senderPort << ".";
  } else { 
    totalBytesSent += bytesSent;
  }
  return totalBytesSent;
}

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
