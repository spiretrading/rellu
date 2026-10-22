#ifndef RELLU_UI_CLIPBOARD_HPP
#define RELLU_UI_CLIPBOARD_HPP
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <QMimeData>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Io/XdClipboard.hpp"
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

    /**
     * The id of the document pasted into, or empty if unknown. Components
     * copied from another document are pasted as new local components.
     */
    std::string m_document;
  };

  /**
   * Holds copies of node subtrees, with the component instances' state
   * trees, the interactions, images and components they use, to paste into
   * any document, and exchanges them with the system clipboard in Adobe
   * XD's clipboard format.
   */
  class Clipboard {
    public:

      /**
       * Constructs an empty Clipboard keeping the files of copied images in
       * get_default_image_directory.
       */
      Clipboard();

      /**
       * Constructs an empty Clipboard.
       * @param directory The folder keeping the files of copied images, which
       *        other applications read, or empty to keep none.
       */
      explicit Clipboard(std::filesystem::path directory);

      ~Clipboard();

      /**
       * Returns the folder in the temporary directory keeping the files of
       * copied images.
       */
      static std::filesystem::path get_default_image_directory();

      /**
       * Deletes the files of copied images from a folder, except those of
       * what system clipboard data holds and of later copies.
       * @param directory The folder keeping the files of copied images.
       * @param data The system clipboard data, or <code>nullptr</code> if
       *        there is none.
       */
      static void remove_stale_images(
        const std::filesystem::path& directory, const QMimeData* data);

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
       * unchanged if none of them can be copied, and deletes the files of
       * the images this application copied before.
       * @param document The document holding the nodes.
       * @param nodes The nodes to copy.
       * @param measurer The function measuring text.
       * @param layout The function providing the stored layout of text.
       * @param source The document the nodes are copied from.
       * @return <code>true</code> iff anything was copied.
       */
      bool copy(const Document& document, const std::vector<NodeId>& nodes,
        const TextMeasurer& measurer = estimate_text_bounds,
        const XdTextLayout& layout = {},
        const XdClipboardSource& source = {});

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
       * unless the data marks what is held: Adobe XD's clipboard format, SVG
       * markup as shapes, an image as an image filled rectangle, or text as
       * a text object, as Adobe XD pastes them.
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
       * Returns the documents the components held are linked to that a
       * paste into another document leaves unlinked, as Adobe XD warns of
       * them after pasting: the document each was copied from, or the one
       * it is linked to, but not a library or the document pasted into.
       * @param target The document pasted into.
       * @return The references to the documents, each once, in the order
       *         of the components held, empty when pasting into the
       *         document copied from.
       */
      std::vector<std::string> get_linked_documents(
        const XdClipboardSource& target) const;

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
      std::filesystem::path m_directory;
      std::shared_ptr<const Content> m_content;

      Clipboard(const Clipboard&) = delete;
      Clipboard& operator =(const Clipboard&) = delete;
      static std::shared_ptr<const Content>& get_published();
  };
}

#endif
