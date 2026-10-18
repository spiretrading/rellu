#ifndef RELLU_UI_DOCUMENT_SESSION_HPP
#define RELLU_UI_DOCUMENT_SESSION_HPP
#include <functional>
#include <QString>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Io/XdGenerator.hpp"
#include "Rellu/Model/Document.hpp"

namespace Rellu {

  /** An open document with its editing state and the file it is saved to. */
  class DocumentSession {
    public:

      /**
       * Constructs a DocumentSession whose document is clean.
       * @param document The document, which must outlive the session's use.
       * @param path The path of the .xd file the document was read from, or
       *        empty for a new document.
       * @param layout The layout stored for text without one when saving.
       */
      DocumentSession(Document& document, QString path, XdTextLayout layout);

      /** Returns the document. */
      Document& get_document() const;

      /** Returns the editor of the document. */
      Editor& get_editor();

      /** Returns the path saved to, or empty if the document is new. */
      const QString& get_path() const;

      /** Returns the name shown for the document, Untitled if it is new. */
      QString get_name() const;

      /**
       * Returns <code>true</code> iff the document changed since it was
       * opened or last saved.
       */
      bool is_dirty() const;

      /**
       * Saves the document as an Adobe XD file, leaving any existing file
       * untouched unless the save completes, and on success makes the path
       * the one saved to and the document clean. Throws XdError on failure.
       * @param path The path of the .xd file.
       */
      void save(const QString& path);

    private:
      Document* m_document;
      QString m_path;
      XdTextLayout m_layout;
      Editor m_editor;

      DocumentSession(const DocumentSession&) = delete;
      DocumentSession& operator =(const DocumentSession&) = delete;
  };

  /** Lists the answers to the prompt about unsaved changes. */
  enum class UnsavedChoice : unsigned char {

    /** Save the changes, then continue. */
    SAVE,

    /** Continue without saving the changes. */
    DISCARD,

    /** Abandon the operation, keeping the document open. */
    CANCEL
  };

  /**
   * Decides whether an operation closing a document may continue, asking
   * what to do with unsaved changes if there are any.
   * @param session The document's session.
   * @param prompt The function asking what to do with the unsaved changes.
   * @param save The function saving the document, returning
   *        <code>true</code> iff it was saved.
   * @return <code>true</code> iff the operation may continue.
   */
  bool resolve_unsaved_changes(const DocumentSession& session,
    const std::function<UnsavedChoice ()>& prompt,
    const std::function<bool ()>& save);

  /**
   * Returns the path a file is saved to, adding the .xd extension unless it
   * already has it.
   * @param path The path chosen.
   */
  QString to_xd_path(const QString& path);
}

#endif
