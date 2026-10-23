#include <memory>
#include <utility>
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QIODevice>
#include <QLocalSocket>
#include "Rellu/Ui/SingleInstance.hpp"

using namespace Rellu;

namespace {
  const auto MAGIC = QByteArray("RLU1");
  constexpr auto HEADER_SIZE = 8;
  constexpr auto ACKNOWLEDGEMENT = '\x06';

  quint32 read_size(const QByteArray& data) {
    auto stream = QDataStream(data.mid(MAGIC.size(), 4));
    auto size = quint32(0);
    stream >> size;
    return size;
  }
}

SingleInstance::SingleInstance(QString key)
  : m_key(std::move(key)) {}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::forward(const QStringList& paths) const {
  auto socket = QLocalSocket();
  socket.connectToServer(m_key);
  if(!socket.waitForConnected(TIMEOUT)) {
    return false;
  }
  socket.write(encode_paths(paths));
  if(!socket.waitForBytesWritten(TIMEOUT)) {
    return false;
  }
  while(socket.bytesAvailable() == 0) {
    if(!socket.waitForReadyRead(TIMEOUT)) {
      return false;
    }
  }
  auto reply = socket.read(1);
  socket.disconnectFromServer();
  return reply == QByteArray(1, ACKNOWLEDGEMENT);
}

bool SingleInstance::listen(
    std::function<void (const QStringList& paths)> handler) {
  m_handler = std::move(handler);
  m_server = std::make_unique<QLocalServer>();
  m_server->setSocketOptions(QLocalServer::UserAccessOption);
  if(!m_server->listen(m_key)) {
    if(m_server->serverError() != QAbstractSocket::AddressInUseError) {
      m_server.reset();
      return false;
    }
    QLocalServer::removeServer(m_key);
    if(!m_server->listen(m_key)) {
      m_server.reset();
      return false;
    }
  }
  QObject::connect(m_server.get(), &QLocalServer::newConnection,
    m_server.get(), [this] {
      while(auto socket = m_server->nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        QObject::connect(socket, &QLocalSocket::disconnected, socket,
          &QObject::deleteLater);
        QObject::connect(socket, &QLocalSocket::readyRead, socket,
          [this, socket, buffer] {
            buffer->append(socket->readAll());
            auto paths = decode_paths(*buffer);
            if(!paths) {
              return;
            }
            buffer->clear();
            socket->write(QByteArray(1, ACKNOWLEDGEMENT));
            socket->flush();
            if(m_handler) {
              m_handler(*paths);
            }
          });
      }
    });
  return true;
}

QString Rellu::make_instance_key() {
  auto hash = QCryptographicHash::hash(
    QDir::homePath().toUtf8(), QCryptographicHash::Sha1);
  return "Rellu-" + QString::fromLatin1(hash.toHex());
}

QByteArray Rellu::encode_paths(const QStringList& paths) {
  auto payload = QByteArray();
  {
    auto stream = QDataStream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << paths;
  }
  auto data = MAGIC;
  {
    auto header = QByteArray();
    auto stream = QDataStream(&header, QIODevice::WriteOnly);
    stream << static_cast<quint32>(payload.size());
    data += header;
  }
  data += payload;
  return data;
}

std::optional<QStringList> Rellu::decode_paths(const QByteArray& data) {
  if(data.size() < HEADER_SIZE || !data.startsWith(MAGIC)) {
    return std::nullopt;
  }
  auto size = read_size(data);
  if(static_cast<quint64>(data.size() - HEADER_SIZE) < size) {
    return std::nullopt;
  }
  auto stream = QDataStream(data.mid(HEADER_SIZE, size));
  stream.setVersion(QDataStream::Qt_6_0);
  auto paths = QStringList();
  stream >> paths;
  if(stream.status() != QDataStream::Ok) {
    return std::nullopt;
  }
  return paths;
}
