#ifndef RELLU_MAIN_WINDOW_HPP
#define RELLU_MAIN_WINDOW_HPP
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <QAbstractButton>
#include <QKeyEvent>
#include <QMainWindow>
#include <QPainter>
#include <QPointF>
#include <QPolygonF>
#include <QMenuBar>
#include <QStackedWidget>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CommandRegistry.hpp"
#include "Rellu/Localization/Localization.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ActionRegistry.hpp"
#include "Rellu/Ui/AppBar.hpp"
#include "Rellu/Ui/ArtboardPresetsPanel.hpp"
#include "Rellu/Ui/AssetsPanel.hpp"
#include "Rellu/Ui/Banner.hpp"
#include "Rellu/Ui/CanvasFrame.hpp"
#include "Rellu/Ui/ComponentActions.hpp"
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/EditActions.hpp"
#include "Rellu/Ui/ExportActions.hpp"
#include "Rellu/Ui/GradientHandles.hpp"
#include "Rellu/Ui/GridActions.hpp"
#include "Rellu/Ui/GuideActions.hpp"
#include "Rellu/Ui/LastProperties.hpp"
#include "Rellu/Ui/LayersPanel.hpp"
#include "Rellu/Ui/PaneFrame.hpp"
#include "Rellu/Ui/PaneResizer.hpp"
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
   * Returns the language of the user interface stored in the preferences,
   * English if there is none.
   */
  Language load_language();

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
      void resizeEvent(QResizeEvent* event) override;
      void showEvent(QShowEvent* event) override;
      void changeEvent(QEvent* event) override;
      bool eventFilter(QObject* watched, QEvent* event) override;
      bool nativeEvent(
        const QByteArray& type, void* message, qintptr* result) override;

    private:
      DocumentView* m_view;
      QWidget* m_menu_row;
      QMenuBar* m_menu_bar;
      QAbstractButton* m_maximize;
      ActionRegistry m_actions;
      CommandRegistry m_commands;
      std::unique_ptr<EditActions> m_edit_actions;
      std::unique_ptr<BannerHost> m_banners;
      std::unique_ptr<ComponentActions> m_component_actions;
      std::unique_ptr<TextActions> m_text_actions;
      std::unique_ptr<GuideActions> m_guide_actions;
      std::unique_ptr<GridActions> m_grid_actions;
      std::unique_ptr<ExportActions> m_export_actions;
      ComponentThumbnails m_thumbnails;
      std::unique_ptr<ComponentDrop> m_component_drop;
      AppBar* m_app_bar;
      Toolbar* m_toolbar;
      PaneFrame* m_left_panel;
      CanvasFrame* m_canvas_frame;
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
      struct GradientTarget {
        GradientFill m_fill;
        GradientFrame m_frame;
      };
      QWidget* m_highlight;
      std::vector<NodeId> m_highlighted;
      QWidget* m_gradient_overlay;
      GradientHandles m_gradient_handles;
      std::unique_ptr<DocumentSession> m_session;
      Subscription m_history_subscription;
      Subscription m_selection_subscription;
      Subscription m_tool_subscription;
      bool m_is_launching;
      bool m_is_restored_expanded;
      double m_left_panel_width;
      PaneResizer* m_pane_resizer;
      std::vector<QWidget*> m_caption_buttons;
      std::optional<QRect> m_restored_bounds;

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
      void update_launch();
      void finish_launch();
      void check_language(Language language);
      void choose_language(Language language);
      void show_message(const QString& message);
      void restore_properties();
      LastProperties get_properties() const;
      void apply_properties(const LastProperties& properties);
      void store_properties();
      void change_properties_id();
      void save_settings() const;
      void update_recent_files();
      void prompt_open();
      bool save_to(const QString& path);
      bool confirm_close();
      UnsavedChoice prompt_unsaved();
      void install(std::unique_ptr<Document> document, const QString& path);
      void detach();
      void update_title();
      void update_document_state();
      void fit_left_panel();
      void set_left_panel_width(double width);
      void apply_left_panel();
      double get_pane_space() const;
      void place_caption_buttons();
      void update_selection_actions();
      void reveal(const NodeId& id);
      void show_region(const Rect& region);
      void reveal_artboard(const Rect& frame);
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
      bool handle_gradient_event(
        ColorPicker::CanvasEvent event, QPointF position);
      bool handle_gradient_key(const QKeyEvent& event);
      Point get_gradient_nudge(Point direction, bool is_large) const;
      void update_gradient_editor();
      std::vector<QPolygonF> get_highlight_outlines() const;
      void update_canvas_color();
      void load_document_settings(GuideSettings guides, GridSettings grids);
      void store_guide_settings() const;
      void store_grid_settings() const;
      void load_export_settings();
      void store_export_settings() const;
  };
}

#endif
