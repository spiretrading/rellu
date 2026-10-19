#ifndef RELLU_UI_APP_BAR_HPP
#define RELLU_UI_APP_BAR_HPP
#include <array>
#include <functional>
#include <optional>
#include <QLabel>
#include <QLineEdit>
#include <QString>
#include <QToolButton>
#include <QWidget>

namespace Rellu {

  /** Lists the workspaces the editor switches between. */
  enum class Workspace : unsigned char {

    /** Drawing and editing the document. */
    DESIGN,

    /** Linking artboards into an interactive prototype. */
    PROTOTYPE,

    /** Publishing the document for review. */
    SHARE
  };

  /**
   * The bar below the menu row: the home button and the workspace switcher
   * at the left, the document's title in the middle, and the invite,
   * preview and zoom controls at the right.
   */
  class AppBar : public QWidget {
    public:

      /**
       * The type of function called when the user switches workspace.
       * @param workspace The workspace switched to.
       */
      using WorkspaceHandler = std::function<void (Workspace workspace)>;

      /**
       * The type of function called when the user enters or picks a zoom.
       * @param zoom The number of canvas pixels per document pixel.
       */
      using ZoomHandler = std::function<void (double zoom)>;

      /** The height of the bar in device independent pixels. */
      static constexpr auto HEIGHT = 42;

      /** The zooms offered by the zoom field's menu. */
      static constexpr auto ZOOM_PRESETS =
        std::to_array({0.25, 0.33, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0});

      /**
       * Constructs an AppBar in the design workspace without a title.
       * @param parent The parent widget.
       */
      explicit AppBar(QWidget* parent = nullptr);

      /** Returns the workspace shown. */
      Workspace get_workspace() const;

      /**
       * Shows a workspace as the current one, without calling the handler.
       * @param workspace The workspace.
       */
      void set_workspace(Workspace workspace);

      /**
       * Sets the function called when the user switches workspace.
       * @param handler The function to call.
       */
      void set_workspace_handler(WorkspaceHandler handler);

      /** Returns the title shown. */
      QString get_title() const;

      /**
       * Shows a document's name as the title.
       * @param name The document's name, or empty for no document.
       * @param is_modified Whether the document has unsaved changes.
       */
      void set_title(const QString& name, bool is_modified);

      /** Returns the zoom shown. */
      double get_zoom() const;

      /**
       * Shows a zoom in the zoom field, without calling the handler.
       * @param zoom The number of canvas pixels per document pixel.
       */
      void set_zoom(double zoom);

      /**
       * Sets the function called when the user enters or picks a zoom.
       * @param handler The function to call.
       */
      void set_zoom_handler(ZoomHandler handler);

      /** Returns the zoom field. */
      QLineEdit& get_zoom_field();

      /**
       * Returns the button switching to a workspace.
       * @param workspace The workspace.
       */
      QToolButton& get_workspace_button(Workspace workspace);

    private:
      Workspace m_workspace;
      double m_zoom;
      WorkspaceHandler m_workspace_handler;
      ZoomHandler m_zoom_handler;
      QToolButton* m_design;
      QToolButton* m_prototype;
      QToolButton* m_share;
      QLabel* m_title;
      QToolButton* m_history;
      QLineEdit* m_zoom_field;

      void update_workspace();
      void enter_zoom();
      void pick_zoom(double zoom);
  };

  /**
   * Formats a zoom as a percentage the way XD's zoom field shows it, with
   * one decimal when the percentage is not whole, such as 30.6%.
   * @param zoom The number of canvas pixels per document pixel.
   */
  QString format_zoom(double zoom);

  /**
   * Parses a zoom entered as a percentage, with or without the percent
   * sign.
   * @param text The text entered.
   * @return The zoom, or <code>std::nullopt</code> if the text is not a
   *         positive percentage.
   */
  std::optional<double> parse_zoom(const QString& text);
}

#endif
