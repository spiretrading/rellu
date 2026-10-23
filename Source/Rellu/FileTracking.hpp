#ifndef RELLU_FILE_TRACKING_HPP
#define RELLU_FILE_TRACKING_HPP
#include <QByteArray>
#include <QString>

namespace Rellu {

  /**
   * Returns the identity the platform gives a file, which it keeps when the
   * file is moved or renamed within its volume.
   * @param path The path of the file.
   * @return The identity, or empty if the platform gives none.
   */
  QByteArray identify_file(const QString& path);

  /**
   * Finds a file by the identity the platform gave it, ignoring files moved
   * to the Recycle Bin.
   * @param identity The file's identity.
   * @return The file's path, or empty if it is not found.
   */
  QString locate_file(const QByteArray& identity);
}

#endif
