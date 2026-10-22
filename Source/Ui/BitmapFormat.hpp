#ifndef RELLU_UI_BITMAP_FORMAT_HPP
#define RELLU_UI_BITMAP_FORMAT_HPP
#include <QString>

namespace Rellu {

  /**
   * Places the data of a MIME type on the Windows clipboard as its
   * device-independent bitmap, the only format the type provides.
   * @param type The MIME type whose data are the bitmap's bytes.
   * @return <code>true</code> iff the application runs on the Windows
   *         platform, the only one the type is placed on.
   */
  bool register_bitmap_format(const QString& type);
}

#endif
