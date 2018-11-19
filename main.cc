
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
  textline = new QLineEdit(this);

  // Lay out the widgets to appear in the main window.
  // For Qt widget and layout concepts see:
  // http://doc.qt.nokia.com/4.7-snapshot/widgets-and-layouts.html
  QVBoxLayout *layout = new QVBoxLayout();
  layout->addWidget(textview);
  layout->addWidget(textline);
  setLayout(layout);

  sock = s;

  // Set unique identifier.
  qsrand((uint) QDateTime::currentMSecsSinceEpoch());
  myOrigin = QString::number(qrand());
  setWindowTitle("P2Papp - " + myOrigin);
  
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

  // QMap items are always sorted by key.

  // Save the message.
  saveMessage(myOrigin, message);

  // Send the rumor to a random neighbor.
  int seqNo = (originsMap[myOrigin].lastKey());
  qsrand(qrand());
  int rand = qrand() % sock->myNeighbors.size();
  sendRumorMessage(myOrigin, seqNo, message, sock->myNeighbors.at(rand));

  // @TODO - Wait for response from the neighbor

  // Clear the textline to get ready for the next input message.
  textline->clear();
}

// Save input sent the chat window. 
void ChatDialog::saveMessage(QString origin, QString text) {
  // Check if we store this origin already.
  if (originsMap.count(origin) > 0) {
    // Get most recent sequence number.
    int seqNo = (originsMap[origin].lastKey() + 1);
    
    // Add entry to the messages mapping.
    originsMap[origin][seqNo] = text;
  } else {
    // We have not seen this origin before - create a new messages map.
    Messages messagesMap;
    messagesMap[1] = text;

    // Add the messages map to the corresponding origin
    originsMap[origin] = messagesMap;
  }
}

// Serialize and propogate a rumor message.
void ChatDialog::sendRumorMessage(QString origin, qint32 seq, QString text, int port) {
  QByteArray buf;
  QDataStream datastream(&buf, QIODevice::ReadWrite);
  QVariantMap message;

  // Serialize the message.
  message["ChatText"] = text;
  message["Origin"] = origin;
  message["SeqNo"] = seq;

  qDebug() << "";
  qDebug() << "Sending message to port: " << port
    << "<\"ChatText\",\"" << message["ChatText"].toString()
    << "\"><\"Origin\",\"" << message["Origin"].toString()
    << "\"><\"SeqNo\",\"" << message["SeqNo"].toString() << "\">";

  datastream << message;

  // Send message to the socket. 
  sock->writeDatagram(&buf, buf.size(), port);
}

// @TODO
// Serialize and propogate a status message.
// void ChatDialog::sendStatusMessage() {

//   QByteArray buf;
//   QDataStream datastream(&buf, QIODevice::ReadWrite);
//   HighestSeqNums wants;
//   // Map "Want" to a nested QMap of HighestSeqNums.
//   QMap<QString, HighestSeqNums> message;

//   // Add 1 to the highest sequence numbers - the "want" message
//   // includes the lowest sequence numbers for each origin from which
//   // the peer has not yet received a message. 
//   for (auto origin : highestSeqNums.keys()) {
//     wants[origin] = highestSeqNums.value(origin) + 1;
//     qDebug() << origin << " " << wants[origin];
//   }

//   // Serialize the message.
//   message["Want"] = wants;

//   datastream << message;

//   // Send message to the socket. 
//   sock->writeDatagram(&buf, buf.size());
// }

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

    // Respond to the "connection" message.
    if (!message["connection"].isNull()) {
      // Send a response to the connection request. 
      if (message["connection"] == "connection request") {
        QByteArray buf;
        QDataStream datastream(&buf, QIODevice::ReadWrite);
        QVariantMap response;

        // Serialize the response.
        response["connection"] = "connection response";

        qDebug() << "Sending a connection response to: " << senderPort;

        datastream << response;

        // Send response to the socket. 
        sock->writeDatagram(&buf, buf.size(), senderPort);
      // Acknowledge the connection response.
      } else if (message["connection"] == "connection response") {
        sock->addNeighbors(senderPort);
      }
      break;
    }

    // Respond to the "rumor" message.
    if (message["Want"].isNull()) {
        qDebug() << "Receiving a \"rumor\" message from: " << senderPort;

      // Serialize the rumor message. 
      QString mText = message["ChatText"].toString();
      QString mOrigin = message["Origin"].toString();
      qint32 mSeqNo = message["SeqNo"].toInt();
      QString messageText = "(" + QString::number(mSeqNo) + "@" + mOrigin + "}: " + mText;

      qDebug() << "";
      qDebug() << "Received rumor: "
        << "<\"ChatText\",\"" << mText
        << "\"><\"Origin\",\"" << mOrigin
        << "\"><\"SeqNo\",\"" << mSeqNo << "\">";

      // @TODO
      ////////////////////////
      // Check if we have seen this message before - if we have, do nothing.
      // if (originsMap.count(mOrigin) > 0 && originsMap[mOrigin].count(mSeqNo) > 0) {
      //   if (originsMap[mOrigin][mSeqNo] != mText) {
      //     qDebug() << "Duplicate/corrupt message received. Ignoring";
      //   } else {
      //     qDebug() << "Duplicate message received. Ignoring";
      //   }
      //   continue;
      // }

      // // Check if we have seen this origin before
      // if (highestSeqNums.count(mOrigin) <= 0) {
      //   highestSeqNums[mOrigin] = 0;
      // }

      // // Check if the message is in the expected order.
      // if (highestSeqNums[mOrigin] != (mSeqNo - 1)) {
      //   qDebug() << "Received out of order message. Ignoring and sending status message";
      //   // The message received is out of order - send out a status message.
      //   sendStatusMessage();
      //   continue;
      // }

      // // Save the message.
      // saveMessage(mOrigin, mSeqNo, mText);

      // // Send the rumor to the neighbors.
      // // forwardMessage(message);

      // // Display the message in the chat dialog window. 
      // textview->append(messageText);
      ////////////////////////

    }
    // Respond to the "status" message.
    else {
        qDebug() << "Receiving a \"status\" message from: " << senderPort;

      // @TODO
      ////////////////////////
      // QMap<QString, QVariant> requestedMessages = message["Want"].toMap();

      // qDebug() << message["Want"].type();

      // qDebug() << "";
      // qDebug() << "Received status: ";
      // for (auto origin : requestedMessages.keys()) {
      //   qDebug() << origin << ": " << requestedMessages.value(origin).toInt();
      // }
      // qDebug() << "";

      // // Check if we store the requested values.
      // for (auto origin : requestedMessages.keys()) {
      //   // Npte: The requested sequence numbers are the NEXT sequence numbers
      //   // that the requesting node needs.
      //   qint32 reqSeqNo = requestedMessages.value(origin).toInt();

      //   // If the requested sequence numbers are equal to or lower then ours
      //   // - send a rumor with the next message in the requested sequence.
      //   if (reqSeqNo <= highestSeqNums[origin]) {
      //     // Serialize the rumor message. 
      //     QString mText = originsMap[origin][reqSeqNo];
      //     QString mOrigin = myOrigin;
      //     qint32 mSeqNo = reqSeqNo;
      //     sendRumorMessage(mOrigin, mSeqNo, mText);
      //   } 
      //   // If the sequence numbers are up to date - do nothing. 
      //   // If the sequence numbers are higher than ours - send out a new
      //   // status message. 
      //   else if (reqSeqNo > (highestSeqNums[origin] + 1)) {
      //     sendStatusMessage();
      //   }
      // }
      ////////////////////////

    }
  }
}

// Helper function to print the originsMap and corresponding messagesMap.
void ChatDialog::prettyPrintMaps() {
  for (auto origin : originsMap.keys()) {
    qDebug() << origin << ": ";
    Messages messages = originsMap.value(origin);
    for (auto message : messages.keys()) {
      qDebug() << message << ", " << messages.value(message);
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
  qDebug() << "Range of ports: " << myPortMin << " - " << myPortMax;

  // Choose 2 neighbors.
  timing = true;
}

// Bind the Netsocket to a port in a range of ports defined above.
bool NetSocket::bind() {
  // Try to bind to each of the range myPortMin...myPortMax in turn.
  for (int port = myPortMin; port <= myPortMax; port++) {
    if (QUdpSocket::bind(port)) {
      qDebug() << "Bound to UDP port " << port;
      myPort = port;
      return true;
    }
  }

  qDebug() << "Oops, no ports in my default range " << myPortMin
    << "-" << myPortMax << " available";
  return false;
}

// Find the 2 closest (quickest responding) neighbors.
// If 2 neighbors cannot be found, randomly select 2 ports.
void NetSocket::getNeighbors() {
  for (int port = myPortMin; port <= myPortMax; port++) {
    if (port != myPort) {
      // Get a list of ports.
      ports.append(port);
      
      QByteArray buf;
      QDataStream datastream(&buf, QIODevice::ReadWrite);
      QVariantMap message;

      // Serialize the message.
      message["connection"] = "connection request";

      qDebug() << "Sending a connection message to: " << port;

      datastream << message;

      // Send message to the socket. 
      writeDatagram(&buf, buf.size(), port);
    }
  }

  // Set timer.
  QTimer::singleShot(TIMEOUT, this, SLOT(timeNeighbors()));
}

// Add neighbors to the stored list.
void NetSocket::addNeighbors(int port) {
  if (myNeighbors.size() < 2) {
    // Redundancy check if timeout has occurred.
    if (timing) {
      qDebug() << "Received connection response from port: " << port;
      myNeighbors.append(port);
    }
  }
}

// Timeout handler for finding neighbors.
void NetSocket::timeNeighbors() {
  timing = false;

  // If ports have not been chosen, randomly select two ports.
  if (myNeighbors.size() < 2) {
    qsrand(qrand());
    if (myNeighbors.size() == 1) {
      // Add 1 random port.
      QList<int> tempPorts = ports;
      tempPorts.removeOne(myNeighbors.at(0));
      int rand = qrand() % tempPorts.size();
      myNeighbors.append(tempPorts.at(rand));
    } else {
      // Select 2 random ports.
      int rand = qrand() % ports.size();
      myNeighbors = ports;
      myNeighbors.removeAt(rand);
    }
  }
  
  qDebug() << "Neighbor ports chosen: " << myNeighbors[0] << ", " << myNeighbors[1];

}

// Send a message to a socket.
qint64 NetSocket::writeDatagram(QByteArray *buf, int bufSize, quint16 port) {
  qint64 bytesSent = 0;
  bytesSent = QUdpSocket::writeDatagram(*buf, QHostAddress::LocalHost, port);
  if (bytesSent < 0 || bytesSent != bufSize) {
    qDebug() << "Error sending full datagram to " << QHostAddress::LocalHost << ":" << port << ".";
  }
  return bytesSent;
}

////////

int main(int argc, char **argv) {
  // Initialize Qt toolkit.
  QApplication app(argc,argv);

  // Create a UDP network socket, bind, and get neighbors.
  NetSocket *sock = new NetSocket();
  if (!sock->bind())
    exit(1);
  sock->getNeighbors();

  // Create an initial chat dialog window.
  ChatDialog *dialog = new ChatDialog(sock);
  dialog->show();

  // Enter the Qt main loop; everything else is event driven.
  return app.exec();
}
