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

    /**
     * The artboard that is selected or holds the selection, or nil. When it
     * is in view, nodes copied from an artboard are pasted into it at the
     * position they had in theirs.
     */
    NodeId m_artboard;

    /**
     * The artboard most recently worked in, or nil. Content from another
     * document copied from an artboard goes into it, at the position it had
     * in its own, when no artboard is selected and it is in view.
     */
    NodeId m_focus;

    /**
     * The region of the document in view, or none to treat the whole
     * document as in view. Content copied from outside the view is pasted
     * at its center.
     */
    std::optional<Rect> m_view;
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
       * along with the images and components the document lacks. As in
       * Adobe XD, nodes copied from an artboard go to the same position in
       * the target's artboard when it is in view; otherwise content copied
       * within the view is placed where it was, in the container it was
       * copied from, and other content at the center of the view, inside the
       * topmost artboard it overlaps. Artboards pasted in place move right
       * of the artboards they would overlap, artboards whose name is taken
       * and groups named by number are given the next free name, and copies
       * of a main component become instances.
       * @param target Where to paste.
       * @return The command, or <code>nullptr</code> if nothing is held.
       */
      std::unique_ptr<Command> make_paste_command(
        const PasteTarget& target = {}) const;

    private:
      struct Content;
      class PasteCommand;
      std::shared_ptr<const Content> m_content;

      Clipboard(const Clipboard&) = delete;
      Clipboard& operator =(const Clipboard&) = delete;
      static std::shared_ptr<const Content>& get_published();
  };
}

#endif
