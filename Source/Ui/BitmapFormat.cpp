#include <QGuiApplication>
#include "BitmapFormat.hpp"
#ifdef Q_OS_WIN
#include <cstring>
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
  class BitmapConverter : public QWindowsMimeConverter {
    public:
      explicit BitmapConverter(QString type)
        : m_type(std::move(type)) {}

      bool canConvertFromMime(const FORMATETC& format,
          const QMimeData* data) const override {
        return format.cfFormat == CF_DIB && data->hasFormat(m_type);
      }

      bool convertFromMime(const FORMATETC& format, const QMimeData* data,
          STGMEDIUM* medium) const override {
        if(!canConvertFromMime(format, data) ||
            !(format.tymed & TYMED_HGLOBAL)) {
          return false;
        }
        auto bytes = data->data(m_type);
        auto global = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
        if(!global) {
          return false;
        }
        std::memcpy(GlobalLock(global), bytes.constData(), bytes.size());
        GlobalUnlock(global);
        medium->tymed = TYMED_HGLOBAL;
        medium->hGlobal = global;
        medium->pUnkForRelease = nullptr;
        return true;
      }

      QList<FORMATETC> formatsForMime(
          const QString& type, const QMimeData* data) const override {
        auto formats = QList<FORMATETC>();
        if(type == m_type && data->hasFormat(m_type)) {
          auto format = FORMATETC();
          format.cfFormat = CF_DIB;
          format.ptd = nullptr;
          format.dwAspect = DVASPECT_CONTENT;
          format.lindex = -1;
          format.tymed = TYMED_HGLOBAL;
          formats.append(format);
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

    private:
      QString m_type;
  };
}
#endif

bool Rellu::register_bitmap_format(const QString& type) {
#ifdef Q_OS_WIN
  if(QGuiApplication::platformName() != "windows") {
    return false;
  }
  static auto converter = new BitmapConverter(type);
  static_cast<void>(converter);
  return true;
#else
  return false;
#endif
}
