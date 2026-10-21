#ifndef RELLU_UI_CLIPBOARD_HPP
#define RELLU_UI_CLIPBOARD_HPP
#include <memory>
#include <optional>
#include <vector>
#include <QMimeData>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Where a paste places what a Clipboard holds. */
  struct PasteTarget {

    /** The selected nodes. */
    std::vector<NodeId> m_selection;

    /** The artboard most recently worked in, or nil. */
    NodeId m_focus;

    /** The container entered for editing, or nil at the top level. */
    NodeId m_container;

    /**
     * The region of the document in view, or none to treat the whole
     * document as in view and keep content where it was copied.
     */
    std::optional<Rect> m_view;

    /**
     * Whether the prototype workspace is shown, where pasting copies every
     * interaction rather than only those within component instances and
     * leaves artboards lacking a grid without one.
     */
    bool m_is_prototyping = false;
  };

  /**
   * Holds copies of node subtrees, with the component instances' state
   * trees, the interactions, images and components they use, to paste into
   * any document, and exchanges them with the system clipboard in Adobe
   * XD's clipboard format.
   */
  class Clipboard {
    public:

      /** Constructs an empty Clipboard. */
      Clipboard();

      ~Clipboard();

      /** Returns <code>true</code> iff nothing is held. */
      bool is_empty() const;

      /**
       * Tests whether copying nodes would copy anything: those on the canvas
       * or in a container that are not references.
       * @param document The document holding the nodes.
       * @param nodes The nodes.
       */
      static bool is_copyable(
        const Document& document, const std::vector<NodeId>& nodes);

      /**
       * Replaces what is held with copies of nodes, keeping the clipboard
       * unchanged if none of them can be copied.
       * @param document The document holding the nodes.
       * @param nodes The nodes to copy.
       * @param measurer The function measuring text.
       * @param layout The function providing the stored layout of text.
       * @return <code>true</code> iff anything was copied.
       */
      bool copy(const Document& document, const std::vector<NodeId>& nodes,
        const TextMeasurer& measurer = estimate_text_bounds,
        const XdTextLayout& layout = {});

      /** Discards what is held. */
      void clear();

      /**
       * Makes the data offered to other applications and windows for what is
       * held: Adobe XD's clipboard format and a mark identifying what is
       * held within this application.
       * @return The data, or <code>nullptr</code> if nothing is held.
       */
      std::unique_ptr<QMimeData> make_mime_data() const;

      /**
       * Tests whether system clipboard data holds content that load accepts.
       * @param data The clipboard data.
       */
      static bool is_loadable(const QMimeData& data);

      /**
       * Replaces what is held with the content of system clipboard data,
       * unless the data marks what is held: Adobe XD's clipboard format, an
       * image as an image filled rectangle, or text as a text object, as
       * Adobe XD pastes them.
       * @param data The clipboard data.
       * @param measurer The function measuring text.
       * @return <code>true</code> iff something is held afterwards.
       */
      bool load(const QMimeData& data,
        const TextMeasurer& measurer = estimate_text_bounds);

      /**
       * Makes a command labelled Paste inserting what is held with fresh ids,
       * every id referring within the copied set replaced by its copy's,
       * along with the images and components the document lacks, placed and
       * named as Adobe XD pastes. Copies of a main component become
       * instances, and copies of artboards lacking a grid get the grid of
       * make_initial_grid for the document's grid defaults.
       * @param target Where to paste.
       * @return The command, or <code>nullptr</code> if nothing is held.
       */
      std::unique_ptr<Command> make_paste_command(
        const PasteTarget& target = {}) const;

      /**
       * Makes a command labelled Paste Appearance giving nodes the
       * appearance of what is held, as Adobe XD's Paste Appearance does when
       * a single object or an image is held.
       * @param nodes The nodes to change.
       * @return The command, or <code>nullptr</code> if nothing applies.
       */
      std::unique_ptr<Command> make_paste_appearance_command(
        std::vector<NodeId> nodes) const;

    private:
      struct Content;
      class PasteCommand;
      class AppearanceCommand;
      std::shared_ptr<const Content> m_content;

      Clipboard(const Clipboard&) = delete;
      Clipboard& operator =(const Clipboard&) = delete;
      static std::shared_ptr<const Content>& get_published();
  };
}

#endif
