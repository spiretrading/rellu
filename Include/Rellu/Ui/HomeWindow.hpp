#ifndef RELLU_UI_HOME_WINDOW_HPP
#define RELLU_UI_HOME_WINDOW_HPP
#include <functional>
#include <memory>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QWidget>
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/DocumentWindows.hpp"
#include "Rellu/Ui/HomeScreen.hpp"

namespace Rellu {

  /**
   * Shows Adobe XD's Home screen in a top-level window of its own, making
   * and opening documents in its place.
   */
  class HomeWindow : public QObject {
    public:

      /** The width Home is shown at by default, in logical pixels. */
      static constexpr auto DEFAULT_WIDTH = 1090;

      /** The height Home is shown at by default, in logical pixels. */
      static constexpr auto DEFAULT_HEIGHT = 729;

      /**
       * The type of function asking for a file to open.
       * @param parent The window asking.
       * @return The path of the file chosen, or empty if none was.
       */
      using OpenDialog = std::function<QString (QWidget& parent)>;

      /**
       * Constructs a HomeWindow, hidden, that quits the application when it
       * is closed while no document is open.
       * @param windows The document windows, which must outlive it.
       */
      explicit HomeWindow(DocumentWindows& windows);

      ~HomeWindow() override;

      /** Returns the Home screen. */
      HomeScreen& get_screen();

      /**
       * Sets the function asking for a file to open.
       * @param dialog The function.
       */
      void set_open_dialog(OpenDialog dialog);

      /**
       * Sets the function called when Home is closed while no document is
       * open, in place of quitting the application.
       * @param handler The function.
       */
      void set_quit_handler(std::function<void ()> handler);

      /** Returns <code>true</code> iff Home is shown. */
      bool is_shown() const;

      /**
       * Shows Home as the window the application launches with.
       * @param geometry The geometry the last document window closed with,
       *        as QWidget::saveGeometry gives it, or empty to show Home at
       *        its default size in the middle of the primary screen.
       */
      void show(const QByteArray& geometry);

      /**
       * Shows Home over a document window at its default size, as the app
       * bar's Home button does.
       * @param window The document window.
       */
      void show_over(const QWidget& window);

      /** Brings Home to the front if it is shown. */
      void activate();

      /** Hides Home. */
      void hide();

      /**
       * Opens a file given to the application, in Home's place if Home is
       * the only window shown.
       * @param path The path of the file.
       * @return <code>true</code> iff the file is open.
       */
      bool open(const QString& path);

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override;

    private:
      DocumentWindows* m_windows;
      std::unique_ptr<HomeScreen> m_screen;
      OpenDialog m_open_dialog;
      std::function<void ()> m_quit_handler;
      Subscription m_subscription;

      const QWidget* get_host() const;
      void leave();
      void update();
  };
}

#endif
