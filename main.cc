
#include <unistd.h>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QPair>
#include <QTimer>
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

  // Set the socket.
  sock = s;

  // Set the unique identifier.
  qsrand((uint) QDateTime::currentMSecsSinceEpoch());
  myOrigin = QString::number(qrand());
  setWindowTitle("P2Papp - " + myOrigin);
  qDebug() << "myOrigin is:" << myOrigin;

  // Set sequence number beginning at 1.
  mySeqNo = 1;

  // Get available ports.
  getPorts();

  // Find up to 2 neighbors.
  getNeighbors();

  // Initialize a map to save the highest sequence numbers seen so far.
  highestSeqNums = QVariantMap();

  // Register a callback on the textline's returnPressed signal
  // so that we can send the message entered by the user.
  connect(textline, SIGNAL(returnPressed()), this, SLOT(gotReturnPressed()));
  connect(s, SIGNAL(readyRead()), this, SLOT(gotMessage()));

  QTimer *antiEntropyTimer = new QTimer();
  connect(antiEntropyTimer, SIGNAL(timeout()), this, SLOT(antiEntropy()));
  antiEntropyTimer->start(10000);
}

// Find the 2 closest (quickest responding) neighbors.
// If 2 neighbors cannot be found, randomly select 2 ports.
void ChatDialog::getNeighbors() {
  // Send a "status" message to all available ports.
  for (auto port : ports) {
    qDebug() << "Sending a connection message to:" << port;
    sendStatusMessage(port);
  }

  // Set timer.
  QTimer::singleShot(TIMEOUT, this, SLOT(timeNeighbors()));
}

// Add neighbors to the stored list.
void ChatDialog::addNeighbors(quint16 port) {
  if (myNeighbors.size() < 2) {
    // Redundancy check if timeout has occurred.
    if (timing) {
      qDebug() << "Received connection response from port:" << port;
      myNeighbors.append(port);
    }
  }
}

// Timeout handler for finding neighbors.
void ChatDialog::timeNeighbors() {
  timing = false;

  qDebug() << "Port finding has timed out.";

  // If ports have not been chosen, randomly select two ports.
  if (myNeighbors.size() < 2) {
    qsrand(qrand());
    if (myNeighbors.size() == 1) {
      // Add 1 random port.
      QList<quint16> tempPorts = ports;
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
  qDebug() << "Neighbor ports chosen:" << myNeighbors[0] << "," << myNeighbors[1];
}

// Get list of available ports. 
void ChatDialog::getPorts() {
  ports = sock->getPorts();
}

// Callback when user presses "Enter" in the textline widget.
// Propogates "rumor" messages. 
void ChatDialog::gotReturnPressed() {
  QString message = textline->text();

  // Add the message to the chat window. 
  textview->append(message);

  // Save the message.
  saveMessage(myOrigin, mySeqNo, message);

  // Send the "rumor" to a random neighbor.
  // qsrand(qrand());
  int rand = qrand() % myNeighbors.size();
  sendRumorMessage(myOrigin, mySeqNo, message, myNeighbors.at(rand));

  // Update the sequence number.
  mySeqNo++;

  // Clear the textline to get ready for the next input message.
  textline->clear();
}

// Save input sent the chat window. 
void ChatDialog::saveMessage(QString origin, qint32 seq, QString text) {
  // Check if we have this origin already.
  if (!originsMap[origin].isEmpty()) {
    // Check if the sequence number is the next sequentially.
    if (seq == highestSeqNums[origin].toInt() + 1) {
      // It is - we're good to save it.
      originsMap[origin][seq] = text;
      highestSeqNums[origin] = (quint32) seq;
    }
  } else {
    // We have not seen this origin before - create a new messages map.
    Messages messagesMap;
    messagesMap[seq] = text;

    // Add the messages map to the corresponding origin
    originsMap[origin] = messagesMap;
    highestSeqNums[origin] = (quint32) seq;
  }
}

// Serialize and propogate a rumor message.
void ChatDialog::sendRumorMessage(QString origin, qint32 seq, QString text, quint16 port) {
  QByteArray buf;
  QDataStream datastream(&buf, QIODevice::ReadWrite);
  QVariantMap message;

  // Serialize the message.
  message["ChatText"] = text;
  message["Origin"] = origin;
  message["SeqNo"] = seq;

  qDebug() << "Sending \"rumor\" message to port:" << port
    << ", <\"ChatText\"," << message["ChatText"].toString()
    << "><\"Origin\"," << message["Origin"].toString()
    << "><\"SeqNo\"," << message["SeqNo"].toString() << ">";

  datastream << message;

  // Send message to the socket.
  sock->writeDatagram(&buf, buf.size(), port);
}

// Serialize and propogate a status message.
void ChatDialog::sendStatusMessage(quint16 senderPort) {
  QByteArray datagram;
  QDataStream datastream(&datagram, QIODevice::ReadWrite);
  QVariantMap message = QVariantMap();
  QVariantMap originsToSeq = QVariantMap();

  for (auto o : highestSeqNums.keys()) {
    originsToSeq[o] = QVariant(highestSeqNums[o].toInt() + 1);
  }

  qDebug() << "Sending \"status\" message to port:" << senderPort;

  message["Want"] = originsToSeq;

  datastream << message;
  
  // Send message to the socket. 
  sock->writeDatagram(&datagram, datagram.size(), senderPort);
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

    // Check if we have a status message.
    if (!message["Want"].isNull()) {
      // We have a "status" message.
      handleStatusMessage(message, senderPort);
    } else if (!message["ChatText"].isNull()) {
      // We have a "rumor" message.
      handleRumorMessage(message, senderPort);
    } else {
      // We have a corrupt or undefined message.
      qDebug() << "Received corrupt or undefined message from " << senderPort;
    }
      
    datagram.clear();
  }
}

void ChatDialog::handleStatusMessage(QVariantMap m, quint16 senderPort) {
  // throughout this method, we refer to the originator of the status message as the remote
  QVariantMap wantMap = m.value("Want").toMap();
  bool allAreEqual = true;

  // map to hold a list of origins the remote knows about, that way we can check against our list
  QVariantMap originsKnownToRemote = QVariantMap();

  qDebug() << "Received \"status\" message from port:" << senderPort;

  for (auto wantOrigin : wantMap.keys()) {
    originsKnownToRemote[wantOrigin] = true;
    quint32 mSeqNo = wantMap.value(wantOrigin).toInt();
    quint32 highestForThisOrigin = highestSeqNums[wantOrigin].toInt();
    if (mSeqNo <= highestForThisOrigin) {
      // They're behind.
      sendRumorMessage(wantOrigin, mSeqNo, originsMap[wantOrigin][mSeqNo], senderPort);
      allAreEqual = false;
      break;
      qDebug() << "they're behind";
    } else if (mSeqNo == highestForThisOrigin + 1) {
      // We're both equal.
      qDebug() << "equal";
    } else {
      // They're ahead of us - we need to send a status.
      sendStatusMessage(senderPort);
      allAreEqual = false;
      break;
      qDebug() << "we're behind";
    }
  }

  // if the number of origins the remote knows about is less than ours, we need to send them
  // the first message from each origin we know about that they don't
  if (originsKnownToRemote.count() < originsMap.count()) {
    for (auto ourOrigin : originsMap.keys()) {
      if (originsKnownToRemote[ourOrigin].isNull()) {
        sendRumorMessage(ourOrigin, 1, originsMap[ourOrigin][1], senderPort);
      }
    }
  }

  // if the remote has more origins that we do, we need to request those in our status message
  if (originsKnownToRemote.count() > originsMap.count()) {
    for (auto theirOrigin : originsKnownToRemote.keys()) {
      if (originsMap.contains(theirOrigin) == false) {
        // we don't know about this origin
        // init this origin to the highest seq #s with a value of 0, effectively asking for the 1st message
        highestSeqNums[theirOrigin] = 0;
        sendStatusMessage(senderPort);
      }
    }
  }

  // If we reached this point, we're all up to date. 
  if (allAreEqual) {
    // Flip a coin.
    int heads = qrand() % 2;

    // If heads, pick a new random neighbor to start rumormongering with. 
    if (heads) {
      for (auto neighbor : myNeighbors) {
        if (neighbor != senderPort) {
          // sendRumorMessage(myOrigin, mySeqNo, originsMap[myOrigin][mySeqNo], neighbor);
          sendStatusMessage(neighbor);
          break;
        }
      }
    }
    // If tails, cease the rumormongering process.
  }
}

void ChatDialog::handleRumorMessage(QVariantMap m, quint16 senderPort) {
    // Serialize the rumor message.
    QString mText = m["ChatText"].toString();
    QString mOrigin = m["Origin"].toString();
    qint32 mSeqNo = m["SeqNo"].toInt();
    QString messageText = mText + " {" + QString::number(mSeqNo) + "@" + mOrigin + "}";

    qDebug() << "Received \"rumor\" message from port:" << senderPort
    << ", <\"ChatText\"," << m["ChatText"].toString()
    << "><\"Origin\"," << m["Origin"].toString()
    << "><\"SeqNo\"," << m["SeqNo"].toString() << ">";

    // Check that this sequence number is in the correct order.
    if (mSeqNo == highestSeqNums[mOrigin].toInt() + 1) {
      // It's in order, now let's check we don't already have it.
      if (originsMap[mOrigin][mSeqNo].isNull()) {
        // wW don't have it; save it.
        saveMessage(mOrigin, mSeqNo, mText);
        // Display the message in the window.
        textview->append(messageText);
        // Propogate the rumor to neighbors.
        sendRumorMessage(mOrigin, mSeqNo, mText, senderPort);
      }
    }
    // Always send a status message.
    sendStatusMessage(senderPort);
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

void ChatDialog::antiEntropy() {
  int randomIndex = qrand() % ports.size();
  int targetPort = ports.at(randomIndex);
  qDebug() << "antiEntropy: starting to rumor with port " << targetPort;
  sendStatusMessage(targetPort);
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
  qDebug() << "Range of ports:" << myPortMin << "-" << myPortMax;
}

// Bind the Netsocket to a port in a range of ports defined above.
bool NetSocket::bind() {
  // Try to bind to each of the range myPortMin... myPortMax in turn.
  for (quint16 port = myPortMin; port <= myPortMax; port++) {
    if (QUdpSocket::bind(port)) {
      qDebug() << "Bound to UDP port:" << port;
      myPort = port;
      return true;
    }
  }

  qDebug() << "Oops, no ports in my default range " << myPortMin
    << "-" << myPortMax << " available";
  return false;
}

// Send a message to a socket.
qint64 NetSocket::writeDatagram(QByteArray *buf, int bufSize, quint16 port) {
  qint64 bytesSent = 0;
  bytesSent = QUdpSocket::writeDatagram(*buf, QHostAddress::LocalHost, port);
  if (bytesSent < 0 || bytesSent != bufSize) {
    qDebug() << "Error sending full datagram to" << QHostAddress::LocalHost << ":" << port << ".";
  }
  return bytesSent;
}

// Return a list of ports.
QList<quint16> NetSocket::getPorts() {
  return ports;
}

// Find neighboring ports.
void NetSocket::findPorts() {
  for (quint16 port = myPortMin; port <= myPortMax; port++) {
    if (port != myPort) {
      // Get a list of ports.
      ports.append(port);
    }
  }
}

////////

int main(int argc, char **argv) {
  // Initialize Qt toolkit.
  QApplication app(argc,argv);

  // Create a UDP network socket, bind, and get list of ports.
  NetSocket *sock = new NetSocket();
  if (!sock->bind())
    exit(1);
  sock->findPorts();

  // Create an initial chat dialog window.
  ChatDialog *dialog = new ChatDialog(sock);
  dialog->show();

  // Enter the Qt main loop; everything else is event driven.
  return app.exec();
}

////////
