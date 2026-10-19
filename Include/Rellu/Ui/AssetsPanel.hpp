#ifndef RELLU_UI_ASSETS_PANEL_HPP
#define RELLU_UI_ASSETS_PANEL_HPP
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Resources.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Ui/ColorPicker.hpp"

namespace Rellu {

  /** Lists the sections of the Document Assets panel. */
  enum class AssetSection : unsigned char {

    /** The document's color swatches. */
    COLORS,

    /** The document's character styles. */
    CHARACTER_STYLES,

    /** The document's components. */
    COMPONENTS,

    /** The document's videos. */
    VIDEOS
  };

  /**
   * Renders a component's master into a square image.
   * @param document The document holding the component.
   * @param component The component.
   * @param size The width and height of the image in pixels.
   * @return The image, or a null image if nothing was rendered.
   */
  using ComponentThumbnailer = std::function<
    QImage (const Document& document, const ComponentId& component, int size)>;

  /** The MIME type of a component dragged out of the Document Assets panel. */
  inline const auto COMPONENT_MIME_TYPE =
    QString("application/x-rellu-component");

  /**
   * The Document Assets panel, listing a document's colors, character styles
   * and components, kept up to date as the document changes, colors and
   * character styles in their groups in Adobe XD's order. With an editor,
   * colors and character styles are added from the selection, applied to it
   * by a click (a color to the border with Alt), selected with Ctrl or
   * Shift, renamed by a double-click, edited, grouped, highlighted on the
   * canvas and deleted, and components can be dragged onto the canvas,
   * renamed, swapped for the selected instances and their main components
   * edited.
   */
  class AssetsPanel : public QWidget {
    public:

      /**
       * Constructs an AssetsPanel without a document.
       * @param parent The parent widget.
       */
      explicit AssetsPanel(QWidget* parent = nullptr);

      /** Returns the document listed, or <code>nullptr</code> if none. */
      Document* get_document() const;

      /**
       * Lists a document's assets.
       * @param document The document, or <code>nullptr</code> for none. It
       *        must outlive this panel or be replaced first.
       */
      void set_document(Document* document);

      /** Returns the editor acting on the assets, or <code>nullptr</code>. */
      Editor* get_editor() const;

      /**
       * Lists the assets of an editor's document and acts on them through
       * the editor.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this panel or be replaced first.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the function rendering the thumbnails of components.
       * @param thumbnailer The function, or empty for placeholders.
       */
      void set_thumbnailer(ComponentThumbnailer thumbnailer);

      /**
       * Sets the function editing a component's main component.
       * @param handler The function.
       */
      void set_main_component_handler(
        std::function<void (const ComponentId& component)> handler);

      /**
       * Returns the labels of the entries listed in a section.
       * @param section The section.
       */
      std::vector<QString> get_entries(AssetSection section) const;

      /** Returns the components listed, in order. */
      std::vector<ComponentId> get_components() const;

      /**
       * Returns the thumbnail shown for a component.
       * @param component The component.
       * @return The thumbnail, a null pixmap until it is rendered.
       */
      QPixmap get_thumbnail(const ComponentId& component) const;

      /** Renders the thumbnails waiting to be rendered. */
      void render_thumbnails();

      /**
       * Makes the menu of a component: Edit Main Component and Rename.
       * @param component The component.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_component_menu(const ComponentId& component, QWidget* parent);

      /**
       * Renames a component.
       * @param component The component.
       * @param name The new name.
       * @return <code>true</code> iff the component was renamed.
       */
      bool rename_component(const ComponentId& component, const QString& name);

      /**
       * Scrolls a component into view and highlights it.
       * @param component The component.
       */
      void reveal(const ComponentId& component);

      /**
       * Returns <code>true</code> iff the panel shows the invitation to add
       * assets that an empty document shows.
       */
      bool is_empty_shown() const;

      /** Returns the color swatches listed, in order. */
      std::vector<SwatchId> get_swatches() const;

      /** Returns the character styles listed, in order. */
      std::vector<CharacterStyleId> get_character_styles() const;

      /**
       * Sets the function listing the font families offered when editing a
       * character style.
       * @param families The function returning the family names.
       */
      void set_font_families(std::function<QStringList ()> families);

      /** Returns the color picker editing color swatches. */
      ColorPicker& get_color_picker();

      /**
       * Sets the function listing the font styles of a family offered when
       * editing a character style.
       * @param styles The function returning the style names of a family.
       */
      void set_font_styles(
        std::function<QStringList (const QString& family)> styles);

      /**
       * Sets the function outlining nodes on the canvas, called with the
       * instances of an asset highlighted, or with none to stop.
       * @param handler The function.
       */
      void set_highlight_handler(
        std::function<void (const std::vector<NodeId>& nodes)> handler);

      /**
       * Sets the function showing a brief message, such as when an asset
       * highlighted has no instance.
       * @param handler The function.
       */
      void set_message_handler(
        std::function<void (const QString& message)> handler);

      /** Returns the colors and character styles selected, in order. */
      const std::vector<NodeId>& get_selected_assets() const;

      /**
       * Selects colors and character styles.
       * @param assets The ids of the assets.
       */
      void set_selected_assets(std::vector<NodeId> assets);

      /**
       * Outlines the visible nodes on the canvas using a color or character
       * style, clearing the selection, as Highlight on canvas does.
       * @param asset The id of the swatch or style.
       * @return <code>true</code> iff an instance was found.
       */
      bool highlight_instances(const NodeId& asset);

      /**
       * Places colors or character styles in a new group named New group,
       * as New group from element and New group from selection do.
       * @param assets The ids of the assets, all of one section.
       * @return The id of the group made, or empty if none was.
       */
      std::string group_assets(const std::vector<NodeId>& assets);

      /**
       * Moves colors or character styles to a group of their section.
       * @param assets The ids of the assets, all of one section.
       * @param group The id of the group, or of the section.
       * @return <code>true</code> iff an asset moved.
       */
      bool move_assets(
        const std::vector<NodeId>& assets, const std::string& group);

      /**
       * Renames a group of assets.
       * @param group The group's id.
       * @param name The new name.
       * @return <code>true</code> iff the group was renamed.
       */
      bool rename_group(const std::string& group, const QString& name);

      /**
       * Deletes colors and character styles.
       * @param assets The ids of the assets.
       * @return <code>true</code> iff an asset was deleted.
       */
      bool delete_assets(const std::vector<NodeId>& assets);

      /**
       * Returns the names of the groups of a section in the order listed.
       * @param section The section.
       */
      std::vector<QString> get_group_names(AssetSection section) const;

      /**
       * Adds the distinct fill and border colors of the selection to the
       * assets, as Object &gt; Add Color to Assets does.
       * @return <code>true</code> iff a color was added.
       */
      bool add_colors();

      /**
       * Adds the distinct character styles of the selected text to the
       * assets, as Object &gt; Add Character Style to Assets does.
       * @return <code>true</code> iff a style was added.
       */
      bool add_character_styles();

      /**
       * Applies a color swatch to the selection, as clicking it does.
       * @param swatch The swatch's id.
       * @param is_border Whether the color is applied to the border rather
       *        than the fill.
       * @return <code>true</code> iff the selection changed.
       */
      bool apply_swatch(const SwatchId& swatch, bool is_border);

      /**
       * Applies a character style to the selected text, as clicking it does.
       * @param style The style's id.
       * @return <code>true</code> iff the selection changed.
       */
      bool apply_character_style(const CharacterStyleId& style);

      /**
       * Replaces a color swatch, recoloring the fills linked to it, the
       * replacements made while its picker is open merging into one step.
       * @param swatch The new swatch.
       * @return <code>true</code> iff the swatch changed.
       */
      bool replace_swatch(const ColorSwatch& swatch);

      /**
       * Replaces a character style, restyling the text linked to it, the
       * replacements made while its editor is open merging into one step.
       * @param style The new style.
       * @return <code>true</code> iff the style changed.
       */
      bool replace_character_style(const CharacterStyle& style);

      /**
       * Renames a color swatch.
       * @param swatch The swatch's id.
       * @param name The new name.
       * @return <code>true</code> iff the swatch was renamed.
       */
      bool rename_swatch(const SwatchId& swatch, const QString& name);

      /**
       * Renames a character style.
       * @param style The style's id.
       * @param name The new name.
       * @return <code>true</code> iff the style was renamed.
       */
      bool rename_character_style(
        const CharacterStyleId& style, const QString& name);

      /**
       * Deletes a color swatch, the fills linked to it keeping their color.
       * @param swatch The swatch's id.
       * @return <code>true</code> iff the swatch was deleted.
       */
      bool delete_swatch(const SwatchId& swatch);

      /**
       * Deletes a character style, the text linked to it keeping its style.
       * @param style The style's id.
       * @return <code>true</code> iff the style was deleted.
       */
      bool delete_character_style(const CharacterStyleId& style);

      /**
       * Opens the color picker editing a color swatch.
       * @param swatch The swatch's id.
       */
      void edit_swatch(const SwatchId& swatch);

      /**
       * Opens the editor of a character style.
       * @param style The style's id.
       */
      void edit_character_style(const CharacterStyleId& style);

      /**
       * Makes the menu of a color swatch: Copy with its hex value, Edit |
       * Highlight on canvas | New group from element | Move to... | Rename,
       * Delete.
       * @param swatch The swatch's id.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_swatch_menu(const SwatchId& swatch, QWidget* parent);

      /**
       * Makes the menu of a character style: Remove font | Edit | Highlight
       * on canvas | New group from element | Move to... | Rename, Delete.
       * @param style The style's id.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_character_style_menu(
        const CharacterStyleId& style, QWidget* parent);

      /**
       * Makes the menu of several colors and character styles selected: New
       * group from selection | Move selected items to | Delete.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_selection_menu(QWidget* parent);

      /**
       * Makes the menu of a group of assets: Create subgroup | Rename group,
       * Ungroup, Delete.
       * @param group The group's id.
       * @param parent The menu's parent.
       * @return The menu.
       */
      QMenu* make_group_menu(const std::string& group, QWidget* parent);

      /**
       * Scrolls a color swatch into view and highlights it.
       * @param swatch The swatch's id.
       */
      void reveal_swatch(const SwatchId& swatch);

      /**
       * Scrolls a character style into view and highlights it.
       * @param style The style's id.
       */
      void reveal_character_style(const CharacterStyleId& style);

    private:
      class StyleEditor;
      Document* m_document;
      Editor* m_editor;
      Subscription m_subscription;
      std::vector<std::vector<QString>> m_entries;
      std::vector<SwatchId> m_swatches;
      std::vector<CharacterStyleId> m_character_styles;
      std::unordered_map<NodeId, QPointer<QWidget>> m_asset_rows;
      std::unordered_map<std::string, QPointer<QWidget>> m_group_rows;
      std::vector<std::vector<QString>> m_group_names;
      std::vector<NodeId> m_selected;
      std::function<QStringList ()> m_font_families;
      std::function<QStringList (const QString&)> m_font_styles;
      std::function<void (const std::vector<NodeId>&)> m_highlight_handler;
      std::function<void (const QString&)> m_message_handler;
      ColorPicker* m_picker;
      SwatchId m_edited_swatch;
      QPointer<StyleEditor> m_style_editor;
      std::vector<ComponentId> m_components;
      QWidget* m_content;
      QVBoxLayout* m_content_layout;
      bool m_is_empty_shown;
      ComponentThumbnailer m_thumbnailer;
      std::function<void (const ComponentId&)> m_main_component_handler;
      std::unordered_map<ComponentId, QPixmap> m_thumbnails;
      std::unordered_map<ComponentId, QPointer<QLabel>> m_thumbnail_labels;
      std::unordered_map<ComponentId, QPointer<QWidget>> m_component_rows;
      std::unordered_set<ComponentId> m_pending;
      QTimer m_thumbnail_timer;

      void attach(Document* document);
      void rebuild();
      void add_section(AssetSection section, const QString& title);
      QWidget* make_component_row(const Component& component);
      QWidget* make_swatch_row(const ColorSwatch& swatch);
      QWidget* make_character_style_row(const CharacterStyle& style);
      void add_entries(AssetSection section, const std::string& group,
        QVBoxLayout& layout, int depth);
      QWidget* make_group_row(const AssetGroup& group, QWidget& members,
        int depth);
      void click_asset(const NodeId& id, Qt::KeyboardModifiers modifiers);
      QMenu* make_asset_menu(const NodeId& id, QWidget* parent);
      void add_move_menu(QMenu& menu, const QString& title,
        const std::vector<NodeId>& assets);
      void start_rename(const NodeId& id, std::function<void (QString)> done);
      void start_group_rename(const std::string& group);
      void update_selection();
      void highlight(const NodeId& id);
      bool run(std::unique_ptr<Command> command, bool is_merged = false);
      void invalidate_thumbnails(const ChangeSet& changes);
  };

  /**
   * Makes the data of a component dragged out of the Document Assets panel.
   * @param component The component.
   */
  QMimeData* make_component_mime_data(const ComponentId& component);

  /**
   * Reads the component dragged out of the Document Assets panel.
   * @param data The data dragged.
   * @return The component, or <code>std::nullopt</code> if the data holds
   *         none.
   */
  std::optional<ComponentId> read_component_mime_data(const QMimeData& data);

  /**
   * Returns the label XD lists a color swatch under: its name, or the hex
   * value of its color when it has none.
   * @param swatch The swatch.
   */
  QString get_swatch_label(const ColorSwatch& swatch);

  /**
   * Returns the label XD lists a character style under, its font and size
   * such as <code>Roboto - 12pt</code> with an em dash.
   * @param style The character style.
   */
  QString get_character_style_label(const CharacterStyle& style);
}

#endif
