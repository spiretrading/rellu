#ifndef RELLU_UI_ASSETS_PANEL_HPP
#define RELLU_UI_ASSETS_PANEL_HPP
#include <vector>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>
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
   * The Document Assets panel, listing a document's colors, character styles
   * and components, kept up to date as the document changes.
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

      /**
       * Returns the labels of the entries listed in a section.
       * @param section The section.
       */
      std::vector<QString> get_entries(AssetSection section) const;

      /**
       * Returns <code>true</code> iff the panel shows the invitation to add
       * assets that an empty document shows.
       */
      bool is_empty_shown() const;

    private:
      Document* m_document;
      Subscription m_subscription;
      std::vector<std::vector<QString>> m_entries;
      QWidget* m_content;
      QVBoxLayout* m_content_layout;
      bool m_is_empty_shown;

      void rebuild();
      void add_section(AssetSection section, const QString& title);
  };

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
