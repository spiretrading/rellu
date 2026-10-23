#ifndef RELLU_UI_RECENT_FILES_HPP
#define RELLU_UI_RECENT_FILES_HPP
#include <QString>
#include <QStringList>

namespace Rellu {

  /** Keeps the paths of the most recently opened files, newest first. */
  class RecentFiles {
    public:

      /** The number of paths kept by default. */
      static constexpr auto DEFAULT_CAPACITY = 10;

      /** Constructs an empty RecentFiles keeping the default capacity. */
      RecentFiles();

      /**
       * Constructs an empty RecentFiles.
       * @param capacity The number of paths kept.
       */
      explicit RecentFiles(int capacity);

      /** Returns the paths, newest first. */
      const QStringList& get_paths() const;

      /**
       * Replaces the paths, dropping duplicates and those beyond capacity.
       * @param paths The paths, newest first.
       */
      void set_paths(const QStringList& paths);

      /**
       * Records a path as the newest, removing any earlier entry for the
       * same file.
       * @param path The path of the file opened.
       */
      void add(const QString& path);

      /**
       * Removes a path.
       * @param path The path to remove.
       */
      void remove(const QString& path);

      /** Removes every path. */
      void clear();

    private:
      int m_capacity;
      QStringList m_paths;
  };

  /**
   * Returns <code>true</code> iff two paths name the same file.
   * @param left The first path.
   * @param right The second path.
   */
  bool is_same_file(const QString& left, const QString& right);
}

#endif
