#ifndef RELLU_UI_ARTBOARD_PRESETS_PANEL_HPP
#define RELLU_UI_ARTBOARD_PRESETS_PANEL_HPP
#include <functional>
#include <optional>
#include <QWidget>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /**
   * Lists Adobe XD's artboard presets under their group titles, creating an
   * artboard of the preset clicked as Adobe XD's property inspector does
   * while the Artboard tool is active.
   */
  class ArtboardPresetsPanel : public QWidget {
    public:

      /**
       * The type of function returning the region of the document in view,
       * or none when it is unknown.
       */
      using ViewProvider = std::function<std::optional<Rect> ()>;

      /**
       * The type of function bringing a region of the document into view.
       * @param region The region in document coordinates.
       */
      using RevealHandler = std::function<void (const Rect& region)>;

      /**
       * Constructs an ArtboardPresetsPanel creating nothing until an editor
       * is set.
       * @param parent The parent widget.
       */
      explicit ArtboardPresetsPanel(QWidget* parent = nullptr);

      /** Returns the editor artboards are created through, if any. */
      Editor* get_editor() const;

      /**
       * Sets the editor artboards are created through.
       * @param editor The editor, or <code>nullptr</code> for none. It must
       *        outlive this object or be replaced first.
       */
      void set_editor(Editor* editor);

      /**
       * Sets the function returning the region of the document in view,
       * which decides where artboards are placed.
       * @param provider The function.
       */
      void set_view_provider(ViewProvider provider);

      /**
       * Sets the function bringing a created artboard into view.
       * @param handler The function.
       */
      void set_reveal_handler(RevealHandler handler);

      /**
       * Creates an artboard of a preset and selects it, as clicking the
       * preset does.
       * @param preset The preset.
       * @return The artboard, or nil if no editor is set.
       */
      NodeId create(const ArtboardPreset& preset);

    private:
      Editor* m_editor;
      ViewProvider m_view_provider;
      RevealHandler m_reveal_handler;

      ArtboardPresetsPanel(const ArtboardPresetsPanel&) = delete;
      ArtboardPresetsPanel& operator =(const ArtboardPresetsPanel&) = delete;
  };
}

#endif
