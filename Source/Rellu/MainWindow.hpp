#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <QMainWindow>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QMenuBar>
#include <QStackedWidget>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/ArtboardPresetsPanel.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/PluginsPanel.hpp"
#include "Rellu/Ui/PreviewWindow.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "Rellu/Ui/RecentFiles.hpp"
#include "Rellu/Ui/TextActions.hpp"
#include "Rellu/Ui/Toolbar.hpp"
#include "ComponentDrop.hpp"
#include "ComponentThumbnails.hpp"
#include "DocumentView.hpp"

namespace Rellu {

  /**
   * The application's top-level window editing one document, laid out as
   * Adobe XD's editor: the menu row, merged into the title bar where the
   * platform allows, the app bar, the toolbar, the left panel, the canvas
   * and the property inspector.
   */
  class MainWindow : public QMainWindow {
    public:

      /**
       * Constructs a MainWindow without a document, restoring the geometry
       * and panel saved when the last MainWindow closed.
       * @param parent The parent widget.
       */
      explicit MainWindow(QWidget* parent = nullptr);

      ~MainWindow() override;

      /** Returns the registry of the window's actions. */
      ActionRegistry& get_actions();

      /**
       * Opens an Adobe XD file in place of the current document, asking
       * first what to do with the current document's unsaved changes and
       * reporting any failure to the user.
       * @param path The path of the file to open.
       * @return <code>true</code> iff the file was opened.
       */
      bool open(const QString& path);

      /**
       * Replaces the current document with a new, empty one, asking first
       * what to do with its unsaved changes.
       * @return <code>true</code> iff the new document was made.
       */
      bool make_new();

      /**
       * Asks to confirm, then replaces the current document with its file's
       * saved contents, discarding its unsaved changes, as File &gt; Revert
       * to Saved does.
       * @return <code>true</code> iff the document was reloaded.
       */
      bool revert();

      /**
       * Saves the current document to its file, asking for a path if it has
       * none.
       * @return <code>true</code> iff the document was saved.
       */
      bool save();

      /**
       * Saves the current document to a file chosen by the user.
       * @return <code>true</code> iff the document was saved.
       */
      bool save_as();

    protected:
      void closeEvent(QCloseEvent* event) override;
      void changeEvent(QEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;
      bool nativeEvent(
        const QByteArray& type, void* message, qintptr* result) override;

    private:
      DocumentView* m_view;
      QWidget* m_menu_row;
      QMenuBar* m_menu_bar;
      QWidget* m_maximize;
      ActionRegistry m_actions;
      CommandRegistry m_commands;
      std::unique_ptr<EditActions> m_edit_actions;
      std::unique_ptr<ComponentActions> m_component_actions;
      std::unique_ptr<TextActions> m_text_actions;
      ComponentThumbnails m_thumbnails;
      std::unique_ptr<ComponentDrop> m_component_drop;
      AppBar* m_app_bar;
      Toolbar* m_toolbar;
      QStackedWidget* m_left_panel;
      AssetsPanel* m_assets;
      LayersPanel* m_layers;
      PluginsPanel* m_plugins;
      PropertyInspector* m_inspector;
      PrototypeInspector* m_prototype_inspector;
      ArtboardPresetsPanel* m_presets;
      PreviewWindow* m_preview;
      QWidget* m_inspector_frame;
      RecentFiles m_recent_files;
      TextMeasurer m_measurer;
      enum class GradientPart : unsigned char {
        NONE,
        CENTER,
        ROTATION,
        STOP
      };
      struct GradientTarget {
        GradientFill m_fill;
        Matrix m_to_view;
        Matrix m_from_view;
      };
      struct GradientDrag {
        GradientPart m_part = GradientPart::NONE;
        int m_stop = 0;
        double m_rotation = 0;
        double m_angle = 0;
        Point m_offset = Point(0, 0);
      };
      static constexpr auto DEFAULT_ROTATION_HANDLE_ANGLE = -90.0;
      QWidget* m_highlight;
      std::vector<NodeId> m_highlighted;
      QWidget* m_gradient_overlay;
      GradientDrag m_gradient_drag;
      double m_rotation_handle_angle;
      std::unique_ptr<DocumentSession> m_session;
      Subscription m_history_subscription;
      Subscription m_selection_subscription;
      Subscription m_tool_subscription;

      QMenuBar* make_layout();
      void add_actions();
      void add_component_actions();
      void add_tool_actions();
      void select_tool(const std::string& name);
      void update_tool();
      void show_panel(const std::string& name);
      void set_workspace(Workspace workspace);
      void toggle_preview();
      void restore_settings();
      void set_language(const std::string& language);
      void save_settings() const;
      void update_recent_files();
      void prompt_open();
      bool save_to(const QString& path);
      bool confirm_close();
      UnsavedChoice prompt_unsaved();
      void install(std::unique_ptr<Document> document, const QString& path);
      void detach();
      void update_title();
      void update_selection_actions();
      void reveal(const NodeId& id);
      void show_region(const Rect& region);
      void update_presets();
      void zoom_to_selection();
      double get_zoom() const;
      void update_zoom();
      void add_asset_actions();
      void update_asset_actions();
      bool is_caption(QPoint position) const;
      bool select_for_context_menu(QPoint position);
      void set_highlights(std::vector<NodeId> nodes);
      std::optional<GradientTarget> find_gradient_target() const;
      void paint_gradient_editor(QPainter& painter);
      QPointF get_rotation_handle(const GradientTarget& target) const;
      bool handle_gradient_event(
        ColorPicker::CanvasEvent event, QPointF position);
      void update_gradient_editor();
      std::vector<QPolygonF> get_highlight_outlines() const;
      void update_grid_actions();
  };
}

#endif
