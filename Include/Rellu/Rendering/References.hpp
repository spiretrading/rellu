#ifndef RELLU_RENDERING_REFERENCES_HPP
#define RELLU_RENDERING_REFERENCES_HPP
#include <map>
#include <vector>
#include <QRectF>
#include <QString>

namespace Rellu {

  /** An item of a document exported as reference images by Adobe XD. */
  struct ReferenceItem {

    /** The position of the item among the exported items. */
    int m_index;

    /** The GUID of the item's node. */
    QString m_guid;

    /** The name of the item's node. */
    QString m_name;

    /** The type of the item's node, such as Artboard, Group or Text. */
    QString m_type;

    /** Whether the item's node is visible. */
    bool m_is_visible;

    /** The bounds of the item in document coordinates. */
    QRectF m_bounds;

    /** The bounds of the item's drawing in document coordinates. */
    QRectF m_draw_bounds;

    /** Maps each scale exported to the absolute path of its image. */
    std::map<int, QString> m_images;
  };

  /** An item and scale that failed to export. */
  struct ReferenceError {

    /** The index of the item that failed to export. */
    int m_index;

    /** The scale that failed to export. */
    int m_scale;

    /** The message describing the failure. */
    QString m_message;
  };

  /** The reference images of a document. */
  struct ReferenceDocument {

    /**
     * The path of the document relative to the reference root, without its
     * .xd extension.
     */
    QString m_path;

    /** The absolute path of the directory holding the reference images. */
    QString m_directory;

    /** The items exported. */
    std::vector<ReferenceItem> m_items;

    /** The exports that failed. */
    std::vector<ReferenceError> m_errors;
  };

  /**
   * Returns the root of the reference images, given by the RELLU_REFERENCES
   * environment variable, or C:/Users/Kranar/Nextcloud/rellu_references when
   * it is not set.
   */
  QString get_reference_root();

  /**
   * Lists the documents exported to a reference root.
   * @param root The reference root.
   * @return The path of each document listed in the root's exported.txt.
   */
  std::vector<QString> list_reference_documents(const QString& root);

  /**
   * Loads the reference images of a document.
   * @param root The reference root.
   * @param path The path of the document relative to the <i>root</i>,
   *        without its .xd extension.
   * @return The document's reference images.
   */
  ReferenceDocument load_reference_document(
    const QString& root, const QString& path);

  /**
   * Loads the reference images of every document exported to a reference
   * root.
   * @param root The reference root.
   * @return The reference images of each document listed in the root's
   *         exported.txt.
   */
  std::vector<ReferenceDocument> load_reference_documents(const QString& root);
}

#endif
