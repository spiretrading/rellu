#ifndef RELLU_UI_ASSET_EXPORT_HPP
#define RELLU_UI_ASSET_EXPORT_HPP
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>
#include <QString>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Io/ExportFormat.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** The largest length of an exported file's name. */
  inline constexpr auto MAXIMUM_EXPORT_NAME_LENGTH = 255;

  /** The largest length of an exported file's path. */
  inline constexpr auto MAXIMUM_EXPORT_PATH_LENGTH = 259;

  /** A size a bitmap export writes its assets at. */
  struct ExportScale {

    /** The number of pixels per document unit. */
    double m_factor;

    /** The name of the size, such as 2x or xhdpi. */
    QString m_name;

    /** The suffix of the files written at the size, such as @2x. */
    QString m_suffix;

    /** The subfolder the files are written to, or empty for none. */
    QString m_folder;
  };

  /** A file an export writes. */
  struct ExportTask {

    /** The node exported. */
    NodeId m_node;

    /** The number of pixels per document unit of a bitmap. */
    double m_scale;

    /** The path of the file. */
    QString m_path;

    /** Whether the bitmap exceeds the largest area Adobe XD exports. */
    bool m_is_too_big;
  };

  /** Lists the ways files of an export fail. */
  enum class ExportFailure : unsigned char {

    /** No file failed. */
    NONE,

    /** A file failed for a reason other than its permissions. */
    GENERIC,

    /** A file to replace is read-only. */
    READ_ONLY,

    /** A file could not be written for lack of permission. */
    ACCESS_DENIED
  };

  /** The outcome of an export. */
  struct ExportResult {

    /** The paths of the files written. */
    std::vector<QString> m_written;

    /** The distinct nodes whose files failed, in the order they failed. */
    std::vector<NodeId> m_failed_nodes;

    /** The paths of the files that failed, one per failed node. */
    std::vector<QString> m_failed_paths;

    /** How the files failed. */
    ExportFailure m_failure = ExportFailure::NONE;

    /** Whether the export was cancelled, its written files deleted. */
    bool m_is_cancelled = false;
  };

  /**
   * Reports an export's progress.
   * @param done The number of files settled.
   * @param total The number of files.
   * @return <code>false</code> to cancel the export.
   */
  using ExportProgress = std::function<bool (int done, int total)>;

  /**
   * Evaluates an export size typed into the Export size field: a positive
   * arithmetic expression of numbers, +, -, *, / and parentheses, optionally
   * followed by x.
   * @param text The text typed.
   * @return The scale, or none if the text is not a positive size.
   */
  std::optional<double> parse_export_scale(const QString& text);

  /**
   * Removes the characters Adobe XD removes from the name of an exported
   * file.
   * @param name The name.
   * @return The <i>name</i> without the characters a file name can not hold,
   *         empty if no valid name remains.
   */
  QString sanitize_file_name(const QString& name);

  /** Makes the names of an export's files unique as Adobe XD does. */
  class ExportNamer {
    public:

      /** Constructs an ExportNamer that has made no names. */
      ExportNamer() = default;

      /**
       * Returns a name differing, ignoring case, from those made before by
       * numbering it.
       * @param name The name wanted, replaced by asset if empty.
       * @param limit The largest length of the name, or -1 for none.
       * @return The unique name.
       */
      QString make(QString name, int limit = -1);

    private:
      std::unordered_map<QString, int> m_bases;
      std::unordered_map<QString, QString> m_names;
  };

  /**
   * Returns the sizes an export writes its files at.
   * @param options The export's options.
   * @return The sizes, a single 1x size for a vector format.
   */
  std::vector<ExportScale> get_export_scales(const ExportOptions& options);
  /**
   * Returns the nodes Export Batch exports: the nodes marked for export, in
   * the order Adobe XD visits them.
   * @param document The document.
   */
  std::vector<NodeId> get_batch_export_nodes(const Document& document);

  /**
   * Tests whether any node is marked for export.
   * @param document The document.
   */
  bool has_batch_export_nodes(const Document& document);

  /**
   * Orders nodes as the layers panel lists them, the topmost first, as
   * Export Selected receives them.
   * @param document The document holding the nodes.
   * @param nodes The nodes.
   * @return The <i>nodes</i> in order, those not on the canvas last.
   */
  std::vector<NodeId> sort_by_layers(
    const Document& document, std::vector<NodeId> nodes);

  /**
   * Returns the artboards Export All Artboards exports.
   * @param document The document.
   */
  std::vector<NodeId> get_export_artboards(const Document& document);

  /**
   * Lists the files an export writes, as Adobe XD lists them.
   * @param document The document holding the nodes.
   * @param nodes The nodes exported.
   * @param options The export's options.
   * @param folder The folder written to.
   * @param document_name The name of the document's file without its
   *        extension, or empty if it is untitled.
   * @param measurer The function measuring text.
   * @return The files written, empty if no node can be exported.
   */
  std::vector<ExportTask> make_export_tasks(const Document& document,
    const std::vector<NodeId>& nodes, const ExportOptions& options,
    const QString& folder, const QString& document_name,
    const TextMeasurer& measurer = estimate_text_bounds);

  /**
   * Lists the files of an export that already exist, by name.
   * @param tasks The files the export writes.
   * @return The distinct names of the existing files, in order.
   */
  std::vector<QString> find_existing_files(
    const std::vector<ExportTask>& tasks);

  /**
   * Writes an export's files, settling each separately as Adobe XD does:
   * the files written are kept when others fail, and deleted along with the
   * folders made for them when the export is cancelled.
   * @param document The document holding the nodes.
   * @param tasks The files written.
   * @param options The export's options.
   * @param measurer The function measuring text.
   * @param progress The function reporting progress, called before the
   *        first file and after each file.
   * @return The outcome.
   */
  ExportResult run_export(const Document& document,
    const std::vector<ExportTask>& tasks, const ExportOptions& options,
    const TextMeasurer& measurer, const ExportProgress& progress);
}

#endif
