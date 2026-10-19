#ifndef RELLU_UI_ASSETS_PANEL_HPP
#define RELLU_UI_ASSETS_PANEL_HPP
#include <functional>
#include <optional>
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
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Resources.hpp"
#include "Rellu/Model/Subscription.hpp"

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
   * and components, kept up to date as the document changes. With an editor,
   * its components can be dragged onto the canvas, renamed, swapped for the
   * selected instances and their main components edited.
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
       * Makes the menu of a component: Edit Main Component, Swap Component
       * and Rename.
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
       * Replaces the selected component instances with instances of a
       * component.
       * @param component The component.
       * @return <code>true</code> iff an instance was replaced.
       */
      bool swap_component(const ComponentId& component);

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

    private:
      Document* m_document;
      Editor* m_editor;
      Subscription m_subscription;
      std::vector<std::vector<QString>> m_entries;
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
