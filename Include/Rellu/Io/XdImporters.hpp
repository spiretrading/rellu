#ifndef RELLU_IO_XD_IMPORTERS_HPP
#define RELLU_IO_XD_IMPORTERS_HPP
#include <filesystem>
#include <optional>
#include <unordered_set>
#include <vector>
#include "Rellu/Io/ImportReport.hpp"
#include "Rellu/Io/XdImporter.hpp"
#include "Rellu/Io/XdObject.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Model/ShapeGeometry.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /**
   * The ids of the resources imported, kept in an import's states so that
   * importers link only to resources that exist.
   */
  struct XdResourceIds {

    /** The image resources. */
    std::unordered_set<ImageId> m_images;

    /** The color swatches. */
    std::unordered_set<SwatchId> m_swatches;

    /** The character styles. */
    std::unordered_set<CharacterStyleId> m_character_styles;
  };

  /**
   * Adds the importer of the properties every node shares: id, name,
   * transform, visibility, lock, opacity, blend mode, isolation, responsive
   * resize constraints and export marking.
   * @param importer The importer to add to.
   */
  void add_header_importer(XdImporter& importer);

  /**
   * Adds the importer of the fill, stroke and effects of shapes and text,
   * and of the background of artboards.
   * @param importer The importer to add to.
   */
  void add_style_importer(XdImporter& importer);

  /**
   * Adds the importer of shape nodes and their geometry.
   * @param importer The importer to add to.
   */
  void add_shape_importer(XdImporter& importer);

  /**
   * Adds the importer of text nodes: content, frame, character and
   * paragraph styles and stored layout.
   * @param importer The importer to add to.
   */
  void add_text_importer(XdImporter& importer);

  /**
   * Adds the importer of group nodes.
   * @param importer The importer to add to.
   */
  void add_group_importer(XdImporter& importer);

  /**
   * Adds the importer of the mask role of groups.
   * @param importer The importer to add to.
   */
  void add_mask_importer(XdImporter& importer);

  /**
   * Adds the importer of the repeat grid role of groups.
   * @param importer The importer to add to.
   */
  void add_repeat_grid_importer(XdImporter& importer);

  /**
   * Adds the importer of the scroll group role of groups. The scroll
   * settings of a reference are consumed only when they repeat those of its
   * source, since a reference cannot override them.
   * @param importer The importer to add to.
   */
  void add_scroll_group_importer(XdImporter& importer);

  /**
   * Adds the importer of the stack and padding roles of groups.
   * @param importer The importer to add to.
   */
  void add_layout_importer(XdImporter& importer);

  /**
   * Adds the importer of artboard nodes: frame, viewport, layout grid and
   * guides, placing their children relative to the artboard's origin.
   * @param importer The importer to add to.
   */
  void add_artboard_importer(XdImporter& importer);

  /**
   * Adds the importer of components, their masters and states, and of the
   * component instance role of groups with its overrides.
   * @param importer The importer to add to.
   */
  void add_component_importer(XdImporter& importer);

  /**
   * Adds the importer of syncRef nodes as references.
   * @param importer The importer to add to.
   */
  void add_reference_importer(XdImporter& importer);

  /**
   * Adds the importer of the prototype: interactions, flows and the home
   * artboard.
   * @param importer The importer to add to.
   */
  void add_prototype_importer(XdImporter& importer);

  /**
   * Adds the importer of the image resources, color swatches and character
   * styles.
   * @param importer The importer to add to.
   */
  void add_resource_importer(XdImporter& importer);

  /**
   * Adds the importer placing the artboards and pasteboard nodes on the
   * canvas.
   * @param importer The importer to add to.
   */
  void add_canvas_importer(XdImporter& importer);

  /** Returns an XdImporter with every built-in importer added. */
  XdImporter make_xd_importer();

  /**
   * Reads the geometry of a shape.
   * @param shape The shape's JSON, the shape object of a shape node.
   * @param import The import the shape belongs to.
   * @return The geometry, or <code>std::nullopt</code> if it is malformed.
   */
  std::optional<ShapeGeometry> read_geometry(XdObject& shape, XdImport& import);

  /**
   * Reads a fill.
   * @param fill The fill's JSON.
   * @param import The import the fill belongs to.
   * @return The fill, or <code>std::nullopt</code> if it is malformed.
   */
  std::optional<Fill> read_fill(XdObject& fill, XdImport& import);

  /**
   * Reads a stroke.
   * @param stroke The stroke's JSON.
   * @return The stroke, or <code>std::nullopt</code> if it is malformed.
   */
  std::optional<Stroke> read_stroke(XdObject& stroke);

  /**
   * Reads a list of filters, each drop and inner shadow becoming its own
   * effect.
   * @param filters The filters' JSON.
   * @return The effects in order, skipping malformed filters.
   */
  std::vector<Effect> read_effects(XdArray& filters);

  /**
   * Reads an interaction of the form {triggerEvent, action, properties},
   * leaving its id and owner nil.
   * @param interaction The interaction's JSON.
   * @return The interaction, or <code>std::nullopt</code> if its trigger or
   *         action is not supported.
   */
  std::optional<Interaction> read_interaction(XdObject& interaction);

  /**
   * Imports an Adobe XD package with every built-in importer.
   * @param package The package to import.
   * @param document The document receiving the nodes and resources.
   * @return The report of the import.
   */
  ImportReport import_xd(const XdPackage& package, Document& document);

  /**
   * Imports an Adobe XD file with every built-in importer.
   * @param path The path of the .xd file.
   * @param document The document receiving the nodes and resources.
   * @return The report of the import, whose read time includes opening the
   *         file.
   */
  ImportReport import_xd(const std::filesystem::path& path, Document& document);
}

#endif
