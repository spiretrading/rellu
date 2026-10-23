#ifndef RELLU_UI_DOCUMENT_WINDOWS_HPP
#define RELLU_UI_DOCUMENT_WINDOWS_HPP
#include <functional>
#include <memory>
#include <vector>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QRect>
#include <QString>
#include <QStringList>
#include <QWidget>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/RecentFiles.hpp"

namespace Rellu {

  /** A top-level window editing one document, kept by DocumentWindows. */
  class DocumentWindow {
    public:
      virtual ~DocumentWindow() = default;

      /** Returns the window's widget. */
      virtual QWidget& get_widget() = 0;

      /** Returns the path of the document's file, or empty if it has none. */
      virtual QString get_path() const = 0;

      /** Returns the name shown for the document. */
      virtual QString get_name() const = 0;

      /**
       * Places the window, neither maximized nor minimized, before it is
       * first shown.
       * @param bounds The window's bounds in logical pixels.
       */
      virtual void place(const QRect& bounds) = 0;

      /**
       * Places the window where another window is, in the same state,
       * before it is first shown.
       * @param host The window whose place the window takes.
       */
      virtual void place(const QWidget& host) = 0;

    protected:

      /** Constructs a DocumentWindow. */
      DocumentWindow() = default;

    private:
      DocumentWindow(const DocumentWindow&) = delete;
      DocumentWindow& operator =(const DocumentWindow&) = delete;
  };

  /**
   * Keeps the application's document windows, one per open document, and
   * the files opened recently, shared by every window.
   */
  class DocumentWindows {
    public:

      /** The number of recently opened files kept. */
      static constexpr auto MAXIMUM_RECENT_FILES = 15;

      /**
       * The type of function making the window of a document, reporting
       * any failure to the user itself.
       * @param path The path of the file to open, or empty for a new
       *        document.
       * @param document The new document, or <code>nullptr</code> when
       *        opening a file.
       * @return The window, not yet shown, or <code>nullptr</code> if no
       *         window was made.
       */
      using Factory = std::function<std::unique_ptr<DocumentWindow> (
        const QString& path, std::unique_ptr<Document> document)>;

      /**
       * The type of function returning the area windows are cascaded in.
       * @return The available area of the screen in logical pixels.
       */
      using AreaProvider = std::function<QRect ()>;

      /**
       * The type of function identifying a file wherever it is later moved
       * or renamed.
       * @param path The path of the file.
       * @return The file's identity, or empty if it has none.
       */
      using FileIdentifier = std::function<QByteArray (const QString& path)>;

      /**
       * The type of function finding an identified file.
       * @param identity The file's identity.
       * @return The file's path, or empty if it is not found.
       */
      using FileLocator = std::function<QString (const QByteArray& identity)>;

      /**
       * Constructs DocumentWindows without windows or recent files, quitting
       * the application once its last window closes and cascading windows
       * in the primary screen's available area.
       */
      DocumentWindows();

      /** Destroys every window still kept. */
      ~DocumentWindows();

      /**
       * Sets the function making the windows of documents opened or made.
       * @param factory The function making a document's window.
       */
      void set_factory(Factory factory);

      /**
       * Sets the function called once the last window is removed, in place
       * of quitting the application.
       * @param handler The function to call.
       */
      void set_empty_handler(std::function<void ()> handler);

      /**
       * Sets the function returning the area windows are cascaded in.
       * @param provider The function returning the area.
       */
      void set_area_provider(AreaProvider provider);

      /**
       * Sets the functions following the files opened recently when they
       * are moved or renamed.
       * @param identifier The function identifying a file.
       * @param locator The function finding an identified file.
       */
      void set_file_tracking(FileIdentifier identifier, FileLocator locator);

      /** Returns the windows in the order they were added. */
      std::vector<DocumentWindow*> get_windows() const;

      /**
       * Returns the windows in the order Adobe XD's Window menu lists them
       * in a window: the others, most recently activated first, then the
       * window itself.
       * @param window The window showing the menu.
       */
      std::vector<DocumentWindow*> get_menu_windows(
        const DocumentWindow& window) const;

      /**
       * Returns the window most recently activated, or <code>nullptr</code>
       * if there are no windows.
       */
      DocumentWindow* get_active() const;

      /**
       * Finds the window of a file.
       * @param path The path of the file.
       * @return The window whose document is saved to the file, or
       *         <code>nullptr</code> if there is none.
       */
      DocumentWindow* find(const QString& path) const;

      /**
       * Opens a file in a new window, or activates the window it is already
       * open in, recording it as the most recently opened file.
       * @param path The path of the file.
       * @param host The window whose place a new window takes, or
       *        <code>nullptr</code> to place it as Adobe XD does.
       * @return <code>true</code> iff the file is open.
       */
      bool open(const QString& path, const QWidget* host = nullptr);

      /**
       * Makes a new document in a new window, as File &gt; New does.
       * @return <code>true</code> iff the document was made.
       */
      bool make_new();

      /**
       * Shows a new document in a new window.
       * @param document The new document.
       * @param host The window whose place the new window takes, or
       *        <code>nullptr</code> to place it as Adobe XD does.
       * @return <code>true</code> iff the window was made.
       */
      bool make_new(
        std::unique_ptr<Document> document, const QWidget* host = nullptr);

      /**
       * Adds a window, placing, showing and activating it. The first window
       * keeps its own place, and each window added while others are open is
       * cascaded as Adobe XD cascades them.
       * @param window The window, taken ownership of.
       * @param host The window whose place the window takes, or
       *        <code>nullptr</code>.
       * @return The window added.
       */
      DocumentWindow& add(std::unique_ptr<DocumentWindow> window,
        const QWidget* host = nullptr);

      /**
       * Removes a window that is closing, destroying it once control
       * returns to the event loop.
       * @param window The window to remove.
       */
      void remove(DocumentWindow& window);

      /**
       * Shows and activates a window, restoring it if it is minimized.
       * @param window The window to activate.
       */
      void activate(DocumentWindow& window);

      /**
       * Closes every window in the order they were added, stopping at the
       * first that declines to close.
       * @return <code>true</code> iff every window closed.
       */
      bool close_all();

      /** Returns the paths of the files opened recently, newest first. */
      const QStringList& get_recent_files() const;

      /**
       * Returns the identities of the files opened recently, in the order of
       * get_recent_files, empty where a file has none.
       */
      QList<QByteArray> get_recent_identities() const;

      /**
       * Replaces the files opened recently.
       * @param paths The paths, newest first.
       */
      void set_recent_files(const QStringList& paths);

      /**
       * Replaces the files opened recently with their identities.
       * @param paths The paths, newest first.
       * @param identities The identities in the order of the paths, empty
       *        where a file has none.
       */
      void set_recent_files(
        const QStringList& paths, const QList<QByteArray>& identities);

      /**
       * Follows the files opened recently that were moved or renamed and
       * identifies those without an identity, keeping files that can not be
       * found, as Adobe XD keeps deleted files listed.
       */
      void locate_recent_files();

      /**
       * Records a file as the most recently opened.
       * @param path The path of the file.
       */
      void add_recent_file(const QString& path);

      /**
       * Removes a file from those opened recently.
       * @param path The path of the file.
       */
      void remove_recent_file(const QString& path);

      /** Removes every file opened recently. */
      void clear_recent_files();

      /** Notifies the observers that a window's name changed. */
      void update();

      /**
       * Subscribes an observer to every change to the windows, their names,
       * the order they were activated in or the files opened recently.
       * @param observer The function called with each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      struct Entry {
        std::unique_ptr<DocumentWindow> m_window;
        QWidget* m_widget;
        QMetaObject::Connection m_connection;
      };
      Factory m_factory;
      std::function<void ()> m_empty_handler;
      AreaProvider m_area_provider;
      FileIdentifier m_identifier;
      FileLocator m_locator;
      std::vector<Entry> m_entries;
      std::vector<DocumentWindow*> m_activations;
      int m_cascades;
      RecentFiles m_recent_files;
      QHash<QString, QByteArray> m_identities;
      std::unique_ptr<QObject> m_filter;
      Signal m_signal;

      DocumentWindows(const DocumentWindows&) = delete;
      DocumentWindows& operator =(const DocumentWindows&) = delete;
      void promote(DocumentWindow& window);
      void record(const QString& path, const QByteArray& identity);
      void on_activated(QWidget& widget);
      void on_destroyed(QObject* object);
  };

  /**
   * Returns the bounds Adobe XD gives a window opened while others are
   * open: a fixed size at a fixed offset into the screen's available area,
   * stepping down and right with each window cascaded before it and
   * starting over once a window would leave the area.
   * @param index The number of windows cascaded before it.
   * @param area The screen's available area in logical pixels.
   * @return The window's bounds in logical pixels.
   */
  QRect get_cascade_bounds(int index, const QRect& area);

  /**
   * Returns the title of a document's window as Adobe XD gives it, the
   * application's name following the document's.
   * @param name The name of the document.
   */
  QString make_window_title(const QString& name);
}

#endif
