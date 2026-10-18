#ifndef RELLU_IO_XD_IMPORTER_HPP
#define RELLU_IO_XD_IMPORTER_HPP
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>
#include <QJsonObject>
#include <QString>
#include "Rellu/Io/ImportReport.hpp"
#include "Rellu/Io/XdObject.hpp"
#include "Rellu/Io/XdPackage.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Node.hpp"
#include "Rellu/Model/NodeHeader.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {
  class XdImport;

  /**
   * Holds at most one value of each type, letting importers keep their own
   * data alongside an import or a node without knowing of one another.
   */
  class XdStates {
    public:

      /**
       * Returns the value of a type, default constructing it on first use.
       * @param <T> The type of value.
       */
      template<typename T>
      T& get();

      /**
       * Finds the value of a type.
       * @param <T> The type of value.
       * @return The value, or <code>nullptr</code> if none was made.
       */
      template<typename T>
      T* find();

    private:
      std::vector<std::pair<std::type_index, std::shared_ptr<void>>> m_states;
  };

  /** Lists the places a node being imported takes in the document. */
  enum class XdNodeRole : unsigned char {

    /** On the canvas or within another node. */
    NODE,

    /** The root of a component's master tree. */
    MASTER,

    /** The root of a component state's tree. */
    STATE
  };

  /** Lists the phases of an import, which run in this order. */
  enum class XdPhase : unsigned char {

    /** Images, color swatches and character styles. */
    RESOURCES,

    /** Component masters and their states. */
    COMPONENTS,

    /** Artboards and pasteboard nodes. */
    CANVAS,

    /** Data linking nodes once every node exists, such as interactions. */
    LINKS
  };

  /** A node being imported. */
  struct XdNode {

    /** The import the node belongs to. */
    XdImport* m_import;

    /** The node's JSON. */
    XdObject* m_json;

    /** The parent being imported, or <code>nullptr</code> if there is none. */
    XdNode* m_parent;

    /** The place the node takes in the document. */
    XdNodeRole m_role;

    /** The node's Adobe XD type, such as shape. */
    QString m_type;

    /** The header the node is inserted with, whose id is final once set. */
    NodeHeader m_header;

    /**
     * The payload the node is inserted with, set by the node type's
     * importer and released once the node is inserted.
     */
    std::optional<Payload> m_payload;

    /** The children to import once the node is inserted, if any. */
    XdArray* m_children = nullptr;

    /** Whether the node was inserted. */
    bool m_is_inserted = false;

    /** The data importers keep for the node. */
    XdStates m_states;
  };

  /**
   * Builds a Document from an Adobe XD package by running importers, each
   * responsible for one node type, one group of node properties or one part
   * of the document. For every node, the importer of its type makes the
   * payload, then every property importer runs in registration order, then
   * the node is inserted, its children are imported and every finisher runs.
   */
  class XdImporter {
    public:

      /**
       * Makes the payload of a node of one type, sets the children to
       * import, and leaves the payload unset to skip the node.
       */
      using NodeImporter = std::function<void (XdNode& node)>;

      /** Imports a group of properties of a node before it is inserted. */
      using PropertyImporter = std::function<void (XdNode& node)>;

      /** Completes a node once it and its children are inserted. */
      using NodeFinisher = std::function<void (XdNode& node)>;

      /** Imports a part of the document. */
      using DocumentImporter = std::function<void (XdImport& import)>;

      /** Constructs an XdImporter without any importers. */
      XdImporter() noexcept;

      /**
       * Sets the importer of a node type, replacing any previous one.
       * @param type The Adobe XD node type, such as shape.
       * @param importer The importer.
       */
      void set_node_importer(QString type, NodeImporter importer);

      /**
       * Finds the importer of a node type.
       * @param type The Adobe XD node type.
       * @return The importer, or <code>nullptr</code> if there is none.
       */
      const NodeImporter* find_node_importer(const QString& type) const;

      /**
       * Adds an importer of node properties, run after those added before.
       * @param importer The importer.
       */
      void add_property_importer(PropertyImporter importer);

      /**
       * Adds a node finisher, run after those added before.
       * @param finisher The finisher.
       */
      void add_finisher(NodeFinisher finisher);

      /**
       * Adds an importer of a part of the document.
       * @param phase The phase the importer runs in, after the importers of
       *        the same phase added before.
       * @param importer The importer.
       */
      void add_document_importer(XdPhase phase, DocumentImporter importer);

      /**
       * Imports a package into a document in a single transaction, then
       * discards the document's undo history. A failure throws, leaving the
       * document unchanged.
       * @param package The package to import.
       * @param document The document receiving the nodes and resources.
       * @return The report of the import.
       */
      ImportReport import(const XdPackage& package, Document& document) const;

    private:
      friend class XdImport;
      std::vector<std::pair<QString, NodeImporter>> m_node_importers;
      std::vector<PropertyImporter> m_property_importers;
      std::vector<NodeFinisher> m_finishers;
      std::vector<std::pair<XdPhase, DocumentImporter>> m_document_importers;
  };

  /** The state of one import shared by every importer. */
  class XdImport {
    public:

      /**
       * Reads the graphic content and interactions of a package.
       * @param importer The importers to run.
       * @param package The package to import.
       * @param document The document receiving the nodes and resources.
       * @param transaction The transaction making every change.
       * @param report The report of the import.
       */
      XdImport(const XdImporter& importer, const XdPackage& package,
        Document& document, Transaction& transaction, ImportReport& report);

      /** Reports the keys of the package's entries left unconsumed. */
      ~XdImport();

      /** Returns the package being imported. */
      const XdPackage& get_package() const;

      /** Returns the document being built. */
      Document& get_document();

      /** Returns the transaction making every change. */
      Transaction& get_transaction();

      /** Returns the report of the import. */
      ImportReport& get_report();

      /** Returns the data importers keep for the import. */
      XdStates& get_states();

      /** Returns the graphic content holding the shared resources. */
      XdObject& get_resources();

      /** Returns the graphic content holding the pasteboard nodes. */
      XdObject& get_pasteboard();

      /** Returns the graphic content of each artboard entry. */
      const std::vector<std::unique_ptr<XdObject>>& get_artboards();

      /** Returns the prototype interactions. */
      XdObject& get_interactions();

      /**
       * Makes a root object for the JSON of a node, reporting its keys under
       * the node's type, such as &lt;shape&gt;.
       * @param json The node's JSON.
       */
      std::unique_ptr<XdObject> make_node_json(const QJsonObject& json);

      /**
       * Imports a node and its descendants.
       * @param json The node's JSON.
       * @param parent The parent being imported, or <code>nullptr</code> to
       *        place the node on the canvas or make it a root.
       * @param role The place the node takes in the document.
       * @return The node's id, or nil if the node was skipped.
       */
      NodeId import_node(XdObject& json, XdNode* parent, XdNodeRole role);

      /**
       * Records a warning.
       * @param message The warning's message.
       */
      void warn(std::string_view message);

    private:
      const XdImporter* m_importer;
      const XdPackage* m_package;
      Document* m_document;
      Transaction* m_transaction;
      ImportReport* m_report;
      XdStates m_states;
      std::unique_ptr<XdObject> m_resources;
      std::unique_ptr<XdObject> m_pasteboard;
      std::vector<std::unique_ptr<XdObject>> m_artboards;
      std::unique_ptr<XdObject> m_interactions;

      XdImport(const XdImport&) = delete;
      XdImport& operator =(const XdImport&) = delete;
  };

  template<typename T>
  T& XdStates::get() {
    if(auto state = find<T>()) {
      return *state;
    }
    auto state = std::make_shared<T>();
    auto& result = *state;
    m_states.emplace_back(std::type_index(typeid(T)), std::move(state));
    return result;
  }

  template<typename T>
  T* XdStates::find() {
    auto type = std::type_index(typeid(T));
    for(auto& state : m_states) {
      if(state.first == type) {
        return static_cast<T*>(state.second.get());
      }
    }
    return nullptr;
  }
}

#endif
