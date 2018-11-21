
#include <unistd.h>
#include <QVBoxLayout>
#include <QApplication>
#include <QDebug>
#include <QDateTime>
#include <QPair>
#include <QTimer>
#include <QColor>
#include <QSignalMapper>
#include <algorithm>
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

  // Initialize a map to save the highest sequence numbers seen so far.
  highestSeqNums = QVariantMap();

  // Register a callback on the textline's returnPressed signal
  // so that we can send the message entered by the user.
  connect(textline, SIGNAL(returnPressed()), this, SLOT(gotReturnPressed()));
  // register callback on sockets' readyRead signal to read a packet
  connect(s, SIGNAL(readyRead()), this, SLOT(gotMessage()));
  
  // begin the process of finding neighbors
  determineNearestNeighbors();

  rumorTiming = false;

  QTimer *antiEntropyTimer = new QTimer();
  connect(antiEntropyTimer, SIGNAL(timeout()), this, SLOT(antiEntropy()));
  antiEntropyTimer->start(10000);
}

// Find the 2 closest (quickest responding) neighbors.
// If 2 neighbors cannot be found, randomly select 2 ports.
void ChatDialog::determineNearestNeighbors() {
  ports = getPorts();

  for (int i = 0; i < NUM_NEIGHBORS; i++) {
    int rand;
    int portToAdd;
    do {
      rand = qrand() % ports.size();
      portToAdd = ports[rand];
    } while(myNeighbors.contains(portToAdd));
    myNeighbors.append(portToAdd);
  }
  QElapsedTimer *timer = new QElapsedTimer();
  const int PING_TIMEOUT = 5000;
  int remaining = PING_TIMEOUT;
  timer->start();
  while (remaining > 0) {
    remaining = PING_TIMEOUT - timer->elapsed();
  }
  
  qDebug() << "myNeighbors are: " << myNeighbors;
}

// Get list of available ports. 
QList<quint16> ChatDialog::getPorts() {
  return sock->getPorts();
}

// Callback when user presses "Enter" in the textline widget.
// Propogates "rumor" messages. 
void ChatDialog::gotReturnPressed() {
  QString message = textline->text();

  // Add the message to the chat window. 
  QString messageText = "<span style=\"color:'red';\"><b>" + myOrigin + "</b></span>: " + message;
  textview->append(messageText);

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

    // Block until we receive a status message from the port we sent a
    // rumor to.
    if (rumorTiming) {
      // We have a "status" message from the correct port.
      if (!message["Want"].isNull() && senderPort == rumorPort) {
        rumorTiming = false;
        qDebug() << "We have received the \"status\" we are waiting for.";
      }
    }

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
  // Throughout this method, we refer to the originator of the status message as the remote
  QVariantMap wantMap = m.value("Want").toMap();
  bool allAreEqual = true;

  // Map to hold a list of origins the remote knows about, that way we can check against our list
  QVariantMap originsKnownToRemote = QVariantMap();

  qDebug() << "Received \"status\" message from port:" << senderPort;

  for (auto wantOrigin : wantMap.keys()) {
    originsKnownToRemote[wantOrigin] = true;
    quint32 mSeqNo = wantMap.value(wantOrigin).toInt();
    quint32 highestForThisOrigin = highestSeqNums[wantOrigin].toInt();
    if (mSeqNo <= highestForThisOrigin) {
      // They're behind.
      qDebug() << "They're behind.";
      sendRumorMessage(wantOrigin, mSeqNo, originsMap[wantOrigin][mSeqNo], senderPort);
      allAreEqual = false;
      break;
    } else if (mSeqNo == highestForThisOrigin + 1) {
      // We're both equal.
      qDebug() << "We're equal.";
    } else {
      // They're ahead of us - we need to send a status.
      qDebug() << "We're behind.";
      sendStatusMessage(senderPort);
      allAreEqual = false;
      break;
    }
  }

  // If the number of origins the remote knows about is less than ours, we need to send them
  // the first message from each origin we know about that they don't.
  if (originsKnownToRemote.count() < originsMap.count()) {
    for (auto ourOrigin : originsMap.keys()) {
      if (originsKnownToRemote[ourOrigin].isNull()) {
        sendRumorMessage(ourOrigin, 1, originsMap[ourOrigin][1], senderPort);
      }
    }
  }

  // If the remote has more origins that we do, we need to request those in our status message.
  if (originsKnownToRemote.count() > originsMap.count()) {
    for (auto theirOrigin : originsKnownToRemote.keys()) {
      if (originsMap.contains(theirOrigin) == false) {
        // We don't know about this origin.
        // Init this origin to the highest seq #s with a value of 0, effectively asking for the 1st message.
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
    QString messageText = "<span style=\"color:'blue';\"><b>" + mOrigin + "</b></span>: " + mText;

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

        // Set values for timeout handler.
        rumorOrigin = mOrigin;
        rumorSeq = mSeqNo;
        rumorText = mText;
        rumorPort = senderPort;
        rumorTiming = true;
        
        // Set timer.
        QTimer::singleShot(TIMEOUT, this, SLOT(timeRumor()));

        return;
      }
    }
    sendStatusMessage(senderPort);
}

// Handle rumor timeout.
void ChatDialog::timeRumor() {
  if (rumorTiming) {
    qDebug() << "Rumor sending has timed out. Resending the rumor.";

    // Resend the rumor to neighbors.
    sendRumorMessage(rumorOrigin, rumorSeq, rumorText, rumorPort);
  }
  rumorTiming = false;
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
QList<quint16> NetSocket::findPorts() {
  for (quint16 port = myPortMin; port <= myPortMax; port++) {
    if (port != myPort) {
      // Get a list of ports.
      ports.append(port);
    }
  }
  return ports;
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
