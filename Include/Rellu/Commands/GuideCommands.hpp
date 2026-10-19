#ifndef RELLU_COMMANDS_GUIDE_COMMANDS_HPP
#define RELLU_COMMANDS_GUIDE_COMMANDS_HPP
#include <functional>
#include <string>
#include <vector>
#include "Rellu/Commands/Command.hpp"
#include "Rellu/Model/Artboard.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /**
   * Returns the artboards Adobe XD's guide commands act on: the selected
   * artboards if any is selected, otherwise the artboards holding the
   * selected nodes.
   * @param document The document holding the selection.
   * @param selection The selected nodes.
   */
  std::vector<NodeId> get_guide_artboards(
    const Document& document, const std::vector<NodeId>& selection);

  /**
   * Tests whether the selection lies within artboards: the first selected
   * node is not an artboard and every selected node is in an artboard.
   * @param document The document holding the selection.
   * @param selection The selected nodes.
   * @param is_multiple Whether the nodes may lie in several artboards.
   * @param has_guides Whether every artboard holding them must have guides.
   */
  bool is_within_artboards(const Document& document,
    const std::vector<NodeId>& selection, bool is_multiple, bool has_guides);

  /**
   * Inserts a guide among others, after those of the other orientation
   * when it runs vertically and before those of its orientation at or past
   * its position.
   * @param guides The guides, horizontal ones first, each orientation in
   *        increasing position.
   * @param guide The guide to insert.
   */
  void insert_guide(std::vector<Guide>& guides, Guide guide);

  /**
   * Merges guides into an artboard's as Adobe XD's Paste Guides does: those
   * at a position no guide of the same orientation has and no further than
   * one unit inside the artboard's far edge are added.
   * @param guides The artboard's guides.
   * @param pasted The guides pasted, keeping their identifiers.
   * @param size The artboard's size.
   * @return The merged guides.
   */
  std::vector<Guide> merge_guides(std::vector<Guide> guides,
    const std::vector<Guide>& pasted, Size size);

  /** Replaces the guides of an artboard, labelled as given. */
  class SetGuidesCommand : public Command {
    public:

      /**
       * Constructs a SetGuidesCommand.
       * @param label The label, such as Add Guide, Move Guide or Delete
       *        Guide.
       * @param artboard The artboard.
       * @param guides The guides.
       */
      SetGuidesCommand(std::string label, const NodeId& artboard,
        std::vector<Guide> guides) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::string m_label;
      NodeId m_artboard;
      std::vector<Guide> m_guides;
  };

  /** Removes the guides of artboards, labelled Clear Guides. */
  class ClearGuidesCommand : public Command {
    public:

      /**
       * Constructs a ClearGuidesCommand.
       * @param artboards The artboards.
       */
      explicit ClearGuidesCommand(std::vector<NodeId> artboards) noexcept;

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_artboards;
  };

  /**
   * Merges guides into artboards with merge_guides, labelled Paste Guides.
   */
  class PasteGuidesCommand : public Command {
    public:

      /**
       * Constructs a PasteGuidesCommand.
       * @param artboards The artboards.
       * @param guides The guides pasted.
       * @param make_id The function making a fresh guide identifier.
       */
      PasteGuidesCommand(std::vector<NodeId> artboards,
        std::vector<Guide> guides, std::function<std::string ()> make_id);

      std::string get_label() const override;
      void apply(const Document& document, Transaction& transaction) override;

    private:
      std::vector<NodeId> m_artboards;
      std::vector<Guide> m_guides;
      std::function<std::string ()> m_make_id;
  };
}

#endif
