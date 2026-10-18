#ifndef RELLU_IO_XD_GENERATOR_HPP
#define RELLU_IO_XD_GENERATOR_HPP
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include "Rellu/Io/XdValue.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /**
   * Computes the lines of a text node that has no stored layout, as Adobe XD
   * stores them.
   */
  using XdTextLayout =
    std::function<std::optional<StoredLayout> (const Text& text)>;

  /**
   * Generates the Adobe XD JSON of a document's nodes and parts from the
   * model alone. The same model state always generates the same JSON.
   * Structural arrays, the children of a node and its component states, are
   * generated as arrays of nulls, one per element, for the writer to fill.
   */
  class XdGenerator {
    public:

      /**
       * Constructs an XdGenerator.
       * @param document The document to generate, which must not change
       *        while the generator is in use.
       * @param layout The layout of text without a stored layout, or empty
       *        to generate such text without a layout.
       */
      XdGenerator(const Document& document, XdTextLayout layout);

      /** Returns the document generated. */
      const Document& get_document() const;

      /**
       * Generates the JSON of a node without its children and states.
       * @param node The node.
       * @return The JSON object.
       */
      XdValue make_node(const Node& node) const;

      /**
       * Returns the component state trees written in a node's JSON.
       * @param node The node.
       * @return The roots of the trees, empty unless the node is a component
       *         master or instance with states.
       */
      std::vector<NodeId> get_states(const Node& node) const;

      /**
       * Generates the root of an artboard or pasteboard entry.
       * @param child_count The number of top-level nodes it holds.
       * @return The JSON object.
       */
      XdValue make_part(std::size_t child_count) const;

      /**
       * Generates the root of the shared resources entry.
       * @param symbols The masters of the components, in the order written.
       * @return The JSON object.
       */
      XdValue make_resources(const std::vector<ComponentId>& symbols) const;

      /** Generates the prototype interactions entry. */
      XdValue make_interactions() const;

      /** Returns the artboards on the canvas, in canvas order. */
      const std::vector<NodeId>& get_artboards() const;

      /** Returns the nodes on the canvas outside artboards, in order. */
      const std::vector<NodeId>& get_pasteboard() const;

      /**
       * Returns a hash of the members of a node's JSON that cached members,
       * such as visualBounds, are derived from.
       * @param node The node's generated JSON.
       */
      static std::uint64_t get_cache_hash(const XdValue& node);

    private:
      struct StateInfo {
        const ComponentState* m_state;
        ComponentId m_component;
      };
      const Document* m_document;
      XdTextLayout m_layout;
      std::vector<NodeId> m_artboards;
      std::vector<NodeId> m_pasteboard;
      std::unordered_map<NodeId, const Component*> m_masters;
      std::unordered_map<NodeId, StateInfo> m_states;
      std::unordered_map<NodeId, std::vector<const Interaction*>>
        m_interactions;
      std::unordered_map<InteractionId, InteractionId> m_inherited;
      std::unordered_map<NodeId, const Flow*> m_flows;
      std::unordered_map<SwatchId, std::size_t> m_legacy_swatches;

      XdGenerator(const XdGenerator&) = delete;
      XdGenerator& operator =(const XdGenerator&) = delete;
      void find_inherited();
      const Overrides* find_scope(const Node& node) const;
      XdValue make_ux(const Node& node) const;
      XdValue make_style(const Node& node, const Style* style) const;
      XdValue make_text(const Node& node, const Text& text) const;
      XdValue make_layout(const Text& text) const;
      XdValue make_node_interaction(const Interaction& interaction) const;
  };

  /**
   * Formats an image id as Adobe XD's resource uid: 32 lowercase hexadecimal
   * digits.
   * @param id The image id.
   */
  std::string to_uid(const ImageId& id);

  /**
   * Formats a path as Adobe XD writes SVG path data.
   * @param path The path.
   */
  std::string to_svg_path(const Path& path);

  /**
   * Returns the swatches written to the legacy colorSwatches list rather
   * than the document library, by position in that list.
   * @param swatches The document's swatches.
   */
  std::unordered_map<SwatchId, std::size_t> find_legacy_swatches(
    const std::vector<ColorSwatch>& swatches);
}

#endif
