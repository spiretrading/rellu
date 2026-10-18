#ifndef RELLU_DOCUMENT_VIEW_HPP
#define RELLU_DOCUMENT_VIEW_HPP
#include <memory>
#include <QPointF>
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Rect.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Rendering/Canvas.hpp"
#include "Rellu/Scene/SceneBuilder.hpp"

namespace Rellu {

  /** Displays a document on the canvas, letting the user pan and zoom. */
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
       * Shows a document, zoomed to fit its contents.
       * @param document The document to show.
       */
      void set_document(std::unique_ptr<Document> document);

      /**
       * Zooms and centers the view so that every node is visible, keeping
       * it so as the view is resized until the user pans or zooms.
       */
      void fit();

    protected:
      void mousePressEvent(QMouseEvent* event) override;
      void mouseMoveEvent(QMouseEvent* event) override;
      void mouseReleaseEvent(QMouseEvent* event) override;
      void wheelEvent(QWheelEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;
      void keyReleaseEvent(QKeyEvent* event) override;
      void resizeEvent(QResizeEvent* event) override;

    private:
      std::unique_ptr<Document> m_document;
      std::unique_ptr<SceneBuilder> m_builder;
      Rect m_bounds;
      double m_zoom;
      Point m_offset;
      double m_level;
      bool m_is_fitting;
      bool m_is_space_pressed;
      bool m_is_panning;
      QPointF m_position;

      void build();
      void zoom(double factor, QPointF center);
      void pan(QPointF delta);
      void update_view();
      void update_cursor();
  };
}

#endif
