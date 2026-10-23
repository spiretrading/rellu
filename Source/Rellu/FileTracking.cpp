#include "FileTracking.hpp"
#include <cstring>
#include <string>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

using namespace Rellu;

#ifdef Q_OS_WIN
namespace {
  constexpr auto SHARING = FILE_SHARE_READ | FILE_SHARE_WRITE |
    FILE_SHARE_DELETE;
  const auto RECYCLE_BIN = QString("\\$Recycle.Bin\\");

  struct Handle {
    HANDLE m_handle;

    explicit Handle(HANDLE handle)
      : m_handle(handle) {}

    ~Handle() {
      if(m_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
      }
    }

    explicit operator bool() const {
      return m_handle != INVALID_HANDLE_VALUE;
    }

    Handle(const Handle&) = delete;
    Handle& operator =(const Handle&) = delete;
  };

  Handle open(const std::wstring& path) {
    return Handle(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, SHARING,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr));
  }

  bool get_id(const Handle& file, FILE_ID_INFO& info) {
    return GetFileInformationByHandleEx(
      file.m_handle, FileIdInfo, &info, sizeof(info));
  }

  QString strip_prefix(QString path) {
    if(path.startsWith("\\\\?\\UNC\\")) {
      return "\\\\" + path.mid(8);
    } else if(path.startsWith("\\\\?\\")) {
      return path.mid(4);
    }
    return path;
  }
}

QByteArray Rellu::identify_file(const QString& path) {
  auto native = QDir::toNativeSeparators(
    QFileInfo(path).absoluteFilePath()).toStdWString();
  auto file = open(native);
  auto info = FILE_ID_INFO();
  if(!file || !get_id(file, info)) {
    return QByteArray();
  }
  auto root = std::wstring(MAX_PATH + 1, L'\0');
  if(!GetVolumePathNameW(
      native.c_str(), root.data(), static_cast<DWORD>(root.size()))) {
    return QByteArray();
  }
  auto identity = QByteArray();
  auto stream = QDataStream(&identity, QIODevice::WriteOnly);
  stream.setVersion(QDataStream::Qt_6_0);
  stream << QString::fromWCharArray(root.c_str()) <<
    static_cast<quint64>(info.VolumeSerialNumber) <<
    QByteArray(reinterpret_cast<const char*>(info.FileId.Identifier),
      sizeof(info.FileId.Identifier));
  return identity;
}

QString Rellu::locate_file(const QByteArray& identity) {
  auto stream = QDataStream(identity);
  stream.setVersion(QDataStream::Qt_6_0);
  auto root = QString();
  auto serial = quint64(0);
  auto id = QByteArray();
  stream >> root >> serial >> id;
  if(stream.status() != QDataStream::Ok || root.isEmpty() ||
      id.size() != static_cast<qsizetype>(sizeof(FILE_ID_128))) {
    return QString();
  }
  auto volume = open(root.toStdWString());
  if(!volume) {
    return QString();
  }
  auto descriptor = FILE_ID_DESCRIPTOR();
  descriptor.dwSize = sizeof(descriptor);
  descriptor.Type = ExtendedFileIdType;
  std::memcpy(
    descriptor.ExtendedFileId.Identifier, id.constData(), id.size());
  auto file = Handle(OpenFileById(volume.m_handle, &descriptor,
    FILE_READ_ATTRIBUTES, SHARING, nullptr, FILE_FLAG_BACKUP_SEMANTICS));
  auto info = FILE_ID_INFO();
  if(!file || !get_id(file, info) || info.VolumeSerialNumber != serial) {
    return QString();
  }
  auto buffer = std::wstring(32768, L'\0');
  auto length = GetFinalPathNameByHandleW(file.m_handle, buffer.data(),
    static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
  if(length == 0 || length >= buffer.size()) {
    return QString();
  }
  auto path = strip_prefix(
    QString::fromWCharArray(buffer.c_str(), static_cast<qsizetype>(length)));
  if(path.contains(RECYCLE_BIN, Qt::CaseInsensitive)) {
    return QString();
  }
  return path;
}
#else
QByteArray Rellu::identify_file(const QString&) {
  return QByteArray();
}

QString Rellu::locate_file(const QByteArray&) {
  return QString();
}
#endif
