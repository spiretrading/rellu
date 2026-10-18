#ifndef RELLU_IO_XD_IMPORTER_HPP
#define RELLU_IO_XD_IMPORTER_HPP
#include <concepts>
#include <functional>
#include <memory>
#include <mutex>
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
#include "Rellu/Model/OpaqueData.hpp"
#include "Rellu/Model/Transaction.hpp"

namespace Rellu {
  class XdImport;

  /**
   * Holds at most one value of each type, letting importers keep their own
   * data alongside an import or a node without knowing of one another.
   * Each import XdImport::import_nodes runs in parallel starts with a fork
   * of its parent's states: a value whose type has a member
   * <code>T fork() const</code> starts as the result of that call, and any
   * other value is shared, so it must be safe to use from several threads.
   * Once they finish, the values each one made or forked are merged back in
   * node order, through the member <code>void merge(T fork)</code> of the
   * parent's value, or kept if the parent has none.
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

      /** Returns the states a parallel import starts with. */
      XdStates fork() const;

      /**
       * Merges back the states of a parallel import.
       * @param states The states, made by fork.
       */
      void merge(XdStates states);

    private:
      struct Entry {
        std::type_index m_type;
        std::shared_ptr<void> m_state;
        std::shared_ptr<void> (*m_fork)(const void*);
        void (*m_merge)(void*, void*);
        bool m_is_own;
      };
      std::vector<Entry> m_states;

      template<typename T>
      static std::shared_ptr<void> fork_state(const void* state);
      template<typename T>
      static void merge_state(void* state, void* fork);
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
    LINKS,

    /** The data the model does not represent, once the document is complete. */
    PRESERVATION
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

  /** A node an import inserted and the JSON it was read from. */
  struct XdNodeRecord {

    /** The node's id. */
    NodeId m_id;

    /** The node's JSON. */
    QJsonObject m_json;

    /**
     * The children whose import failed, each with the id of the imported
     * child it precedes, nil if no imported child follows it.
     */
    std::vector<std::pair<NodeId, QJsonObject>> m_orphans;
  };

  /**
   * Builds a Document from an Adobe XD package by running importers, each
   * responsible for one node type, one group of node properties or one part
   * of the document. For every node, the importer of its type makes the
   * payload, then every property importer runs in registration order, then
   * the node is inserted, its children are imported and every finisher runs.
   * The node importers, property importers and finishers of nodes imported
   * by XdImport::import_nodes run on several threads at once, each node
   * reaching the rest of the import only through its own XdImport.
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

  /**
   * The state of one import shared by every importer. The nodes imported by
   * import_nodes are each imported by a parallel import of their own, whose
   * nodes reach the document only once import_nodes returns: until then,
   * they are found through find_node rather than the document, and there is
   * no transaction.
   */
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

      /**
       * Returns the transaction making every change, throwing
       * std::logic_error within a parallel import.
       */
      Transaction& get_transaction();

      /** Returns the report of the import. */
      ImportReport& get_report();

      /** Returns the data importers keep for the import. */
      XdStates& get_states();

      /**
       * Returns the mutex guarding the graphic content and interactions
       * shared by parallel imports.
       */
      std::mutex& get_mutex();

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
       * Imports nodes without a parent and their descendants on every
       * processor, with the same result as importing each in turn.
       * @param nodes The nodes' JSON.
       * @param role The place the nodes take in the document.
       * @param path The normalized path their keys are reported under, or
       *        empty to report them under their node type.
       * @return The nodes' ids, nil for those skipped.
       */
      std::vector<NodeId> import_nodes(const std::vector<QJsonObject>& nodes,
        XdNodeRole role, std::string_view path = {});

      /**
       * Finds a node imported so far.
       * @param id The node's id.
       * @return The node, or <code>nullptr</code> if there is none, valid
       *         until the next node is imported.
       */
      const Node* find_node(const NodeId& id);

      /**
       * Replaces the payload of a node this import inserted.
       * @param id The node's id.
       * @param payload The payload.
       */
      void replace_payload(const NodeId& id, Payload payload);

      /**
       * Records a warning.
       * @param message The warning's message.
       */
      void warn(std::string_view message);

      /** Returns the nodes inserted so far, with their JSON. */
      const std::vector<XdNodeRecord>& get_records() const;

      /** Returns the opaque data set for the document. */
      const OpaqueData& get_opaque() const;

      /**
       * Sets the opaque data given to the document once the import commits.
       * @param opaque The data.
       */
      void set_opaque(OpaqueData opaque);

    private:
      struct Batch;
      const XdImporter* m_importer;
      const XdPackage* m_package;
      Document* m_document;
      Transaction* m_transaction;
      ImportReport* m_report;
      XdImport* m_main;
      XdStates m_states;
      std::unique_ptr<XdObject> m_resources;
      std::unique_ptr<XdObject> m_pasteboard;
      std::vector<std::unique_ptr<XdObject>> m_artboards;
      std::unique_ptr<XdObject> m_interactions;
      std::vector<XdNodeRecord> m_records;
      OpaqueData m_opaque;
      std::unique_ptr<Batch> m_batch;
      std::mutex m_mutex;

      explicit XdImport(XdImport& main);
      XdImport(const XdImport&) = delete;
      XdImport& operator =(const XdImport&) = delete;
      std::unique_ptr<XdObject> make_root(
        const QJsonObject& json, std::string_view path);
      NodeId insert(XdNode& node);
      bool merge(XdImport& batch);
  };

  template<typename T>
  T& XdStates::get() {
    if(auto state = find<T>()) {
      return *state;
    }
    auto state = std::make_shared<T>();
    auto& result = *state;
    auto entry =
      Entry(std::type_index(typeid(T)), std::move(state), nullptr, nullptr,
        true);
    if constexpr(requires(const T& value) {
        { value.fork() } -> std::same_as<T>; }) {
      entry.m_fork = &fork_state<T>;
    }
    if constexpr(requires(T& value, T fork) {
        value.merge(std::move(fork)); }) {
      entry.m_merge = &merge_state<T>;
    }
    m_states.push_back(std::move(entry));
    return result;
  }

  template<typename T>
  T* XdStates::find() {
    auto type = std::type_index(typeid(T));
    for(auto& state : m_states) {
      if(state.m_type == type) {
        return static_cast<T*>(state.m_state.get());
      }
    }
    return nullptr;
  }

  template<typename T>
  std::shared_ptr<void> XdStates::fork_state(const void* state) {
    return std::make_shared<T>(static_cast<const T*>(state)->fork());
  }

  template<typename T>
  void XdStates::merge_state(void* state, void* fork) {
    static_cast<T*>(state)->merge(std::move(*static_cast<T*>(fork)));
  }
}

#endif
