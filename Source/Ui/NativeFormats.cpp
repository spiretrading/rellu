#include <QGuiApplication>
#include "NativeFormats.hpp"
#ifdef Q_OS_WIN
#include <cstring>
#include <QByteArray>
#include <QList>
#include <QMimeData>
#include <QVariant>
#include <QWindowsMimeConverter>
#include <windows.h>
#include <objidl.h>
#endif

using namespace Rellu;

#ifdef Q_OS_WIN
namespace {
  const auto TEXT_TYPE = QStringLiteral("text/plain");

  bool is_native(const QMimeData& data, const QString& type) {
    return data.formats().contains(type) && !data.hasFormat(type);
  }

  FORMATETC make_format(CLIPFORMAT format) {
    auto result = FORMATETC();
    result.cfFormat = format;
    result.ptd = nullptr;
    result.dwAspect = DVASPECT_CONTENT;
    result.lindex = -1;
    result.tymed = TYMED_HGLOBAL;
    return result;
  }

  bool set_global(const void* data, std::size_t size, STGMEDIUM* medium) {
    auto global = GlobalAlloc(GMEM_MOVEABLE, size);
    if(!global) {
      return false;
    }
    std::memcpy(GlobalLock(global), data, size);
    GlobalUnlock(global);
    medium->tymed = TYMED_HGLOBAL;
    medium->hGlobal = global;
    medium->pUnkForRelease = nullptr;
    return true;
  }

  class NativeConverter : public QWindowsMimeConverter {
    public:
      bool canConvertFromMime(const FORMATETC& format,
          const QMimeData* data) const override {
        if(!(format.tymed & TYMED_HGLOBAL)) {
          return false;
        } else if(format.cfFormat == CF_UNICODETEXT) {
          return is_native(*data, TEXT_TYPE);
        } else if(format.cfFormat == CF_DIB) {
          return is_native(*data, get_native_bitmap_type());
        }
        return false;
      }

      bool convertFromMime(const FORMATETC& format, const QMimeData* data,
          STGMEDIUM* medium) const override {
        if(!canConvertFromMime(format, data)) {
          return false;
        }
        if(format.cfFormat == CF_DIB) {
          auto bytes = data->data(get_native_bitmap_type());
          if(bytes.isEmpty()) {
            return false;
          }
          return set_global(bytes.constData(), bytes.size(), medium);
        }
        auto text = data->text();
        text.append(QChar(0));
        return set_global(
          text.utf16(), text.size() * sizeof(char16_t), medium);
      }

      QList<FORMATETC> formatsForMime(
          const QString& type, const QMimeData* data) const override {
        auto formats = QList<FORMATETC>();
        if(type == TEXT_TYPE && is_native(*data, TEXT_TYPE)) {
          formats.append(make_format(CF_UNICODETEXT));
        } else if(type == get_native_bitmap_type() &&
            is_native(*data, type)) {
          formats.append(make_format(CF_DIB));
        }
        return formats;
      }

      bool canConvertToMime(
          const QString& type, IDataObject* object) const override {
        return false;
      }

      QVariant convertToMime(const QString& type, IDataObject* object,
          QMetaType preferred) const override {
        return QVariant();
      }

      QString mimeForFormat(const FORMATETC& format) const override {
        return QString();
      }
  };
}
#endif

QString Rellu::get_native_bitmap_type() {
  return QStringLiteral("UniformResourceLocator");
}

bool Rellu::register_native_formats() {
#ifdef Q_OS_WIN
  if(QGuiApplication::platformName() != "windows") {
    return false;
  }
  static auto converter = new NativeConverter();
  static_cast<void>(converter);
  return true;
#else
  return false;
#endif
}
