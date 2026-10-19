#ifndef RELLU_DOCUMENT_VIEW_HPP
#define RELLU_DOCUMENT_VIEW_HPP
#include <memory>
#include <optional>
#include <QCursor>
#include <QPointF>
#include <QTimer>
#include <QVariant>
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Rendering/Canvas.hpp"
#include "Rellu/Scene/Scene.hpp"
#include "Rellu/Tools/TextTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"

namespace Rellu {

  /**
   * Displays a document on the canvas, letting the user pan and zoom, and
   * edit it with the active tool through an Editor. The tools include a
   * TextTool named text, chosen with T, which receives keys ahead of the
   * application's shortcuts and input method events while it edits text.
   */
  class DocumentView : public Canvas {
    public:

      /**
       * Constructs a DocumentView showing an empty canvas.
       * @param parent The parent widget.
       */
      explicit DocumentView(QWidget* parent = nullptr);

      ~DocumentView() override;

      /** Returns the document shown, or <code>nullptr</code> if none. */
      const Document* get_document() const;

      /**
       * Shows a document, zoomed to fit its contents, edited through the
       * Editor set with set_editor when it edits this document, or through
       * an Editor of the view's own when none is set.
       * @param document The document to show.
       */
      void set_document(std::unique_ptr<Document> document);

      /**
       * Returns the Editor the document is edited through, or
       * <code>nullptr</code> if none edits the document shown.
       */
      Editor* get_editor() const;

      /**
       * Sets the Editor shared with the rest of the application that the
       * document is edited through, in place of the view's own. The view
       * edits nothing while the Editor edits another document than the one
       * shown, as when it is set just before that document.
       * @param editor The Editor, which must remain until it is replaced or
       *        detached, or <code>nullptr</code> to detach it.
       */
      void set_editor(Editor* editor);

      /**
       * Returns the tools the document is edited with, or
       * <code>nullptr</code> if no Editor edits the document shown.
       */
      ToolManager* get_tools() const;

      /**
       * Zooms and centers the view so that every node is visible, keeping
       * it so as the view is resized until the user pans or zooms.
       */
      void fit();

      /**
       * Zooms and centers the view so that a region of the document is
       * visible.
       * @param region The region in document coordinates.
       */
      void zoom_to(const Rect& region);

      /**
       * Zooms about the center of the view.
       * @param zoom The number of widget pixels per document pixel.
       */
      void set_zoom(double zoom);

    protected:
      bool event(QEvent* event) override;
      void render(QRhiCommandBuffer* buffer) override;
      void mousePressEvent(QMouseEvent* event) override;
      void mouseMoveEvent(QMouseEvent* event) override;
      void mouseReleaseEvent(QMouseEvent* event) override;
      void mouseDoubleClickEvent(QMouseEvent* event) override;
      void wheelEvent(QWheelEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;
      void keyReleaseEvent(QKeyEvent* event) override;
      void inputMethodEvent(QInputMethodEvent* event) override;
      QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
      void leaveEvent(QEvent* event) override;
      void resizeEvent(QResizeEvent* event) override;

    private:
      std::unique_ptr<Document> m_document;
      std::unique_ptr<Scene> m_scene;
      std::unique_ptr<Editor> m_own_editor;
      Editor* m_shared_editor;
      Editor* m_editor;
      std::unique_ptr<ToolManager> m_tools;
      Subscription m_subscription;
      Subscription m_tool_subscription;
      double m_zoom;
      Point m_offset;
      double m_level;
      bool m_is_fitting;
      bool m_is_space_pressed;
      bool m_is_panning;
      QPointF m_position;
      std::optional<QCursor> m_rotate_cursor;
      std::optional<QCursor> m_zoom_in_cursor;
      std::optional<QCursor> m_zoom_out_cursor;
      QTimer m_preparation;
      QTimer m_caret_timer;

      void frame(const Rect& region);
      void zoom(double factor, QPointF center);
      void pan(QPointF delta);
      void update_view();
      void update_cursor();
      void attach_tools();
      TextTool* get_text_tool() const;
      void update_text_editing();
      bool forward(QMouseEvent* event, auto handler);
  };
}

#endif
