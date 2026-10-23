#ifndef RELLU_UI_NATIVE_FORMATS_HPP
#define RELLU_UI_NATIVE_FORMATS_HPP
#include <QString>

namespace Rellu {

  /**
   * Returns the MIME type clipboard data lists the bytes of a
   * device-independent bitmap under to place them on the Windows clipboard
   * as that bitmap only.
   */
  QString get_native_bitmap_type();

  /**
   * Places the text and device-independent bitmap of clipboard data that
   * lists them under text/plain and get_native_bitmap_type without reporting
   * them through hasFormat on the Windows clipboard as Unicode text and that
   * bitmap only, the formats Adobe XD places them as, leaving Windows to
   * derive the others.
   * @return <code>true</code> iff the application runs on the Windows
   *         platform, the only one they are placed on.
   */
  bool register_native_formats();
}

#endif
