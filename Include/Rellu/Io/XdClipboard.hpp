#ifndef RELLU_IO_XD_CLIPBOARD_HPP
#define RELLU_IO_XD_CLIPBOARD_HPP
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <QByteArray>
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** The name of the Windows clipboard format Adobe XD copies through. */
  constexpr auto XD_CLIPBOARD_FORMAT = std::string_view("com.adobe.xd");

  /** A node written to Adobe XD's clipboard format. */
  struct XdClipboardItem {

    /** The node's id. */
    NodeId m_id;

    /** The transform from the node's local space to document space. */
    Matrix m_transform;

    /**
     * The position of the top left of the node's bounds within the artboard
     * holding it, or none for an artboard or a node on the pasteboard.
     */
    std::optional<Point> m_offset;
  };

  /** Identifies the document nodes are copied from. */
  struct XdClipboardSource {

    /** The document's id, or empty to identify no document. */
    std::string m_id;

    /** The path of the document's file, or empty if it has none. */
    std::string m_path;
  };

  /** What reading Adobe XD's clipboard format provides beyond the nodes. */
  struct XdClipboardContent {

    /**
     * The position of the top left of the bounds of each node on the canvas
     * that is not an artboard, in canvas order, within the artboard it was
     * copied from, or none for a node copied from the pasteboard.
     */
    std::vector<std::optional<Point>> m_offsets;

    /** The region in document coordinates the nodes covered when copied. */
    std::optional<Rect> m_bounds;

    /**
     * The id of the document the nodes were copied from, or empty if it is
     * not recorded.
     */
    std::string m_document;
  };

  /**
   * Writes nodes in Adobe XD's clipboard format: UTF-16 JSON, terminated by
   * a null character, listing the nodes in document coordinates with the
   * definitions of the components they use and the region they cover.
   * @param document The document holding the nodes.
   * @param items The nodes in z order, none a descendant of another.
   * @param bounds The region in document coordinates the nodes cover.
   * @param layout The function providing the stored layout of text.
   * @param directory The folder receiving a file for each image the nodes
   *        use, which the data refers to, or empty to refer to no image.
   * @param source The document the nodes are copied from.
   * @return The clipboard data.
   */
  QByteArray write_xd_clipboard(const Document& document,
    const std::vector<XdClipboardItem>& items, const Rect& bounds,
    const XdTextLayout& layout = {},
    const std::filesystem::path& directory = {},
    const XdClipboardSource& source = {});

  /**
   * Reads data in Adobe XD's clipboard format into a document, which
   * receives the nodes on its canvas at their document coordinates. Throws
   * std::invalid_argument if the data is not in the format.
   * @param data The clipboard data.
   * @param document The empty document receiving the nodes.
   * @return What the data records of where the nodes were copied from.
   */
  XdClipboardContent read_xd_clipboard(
    const QByteArray& data, Document& document);
}

#endif
