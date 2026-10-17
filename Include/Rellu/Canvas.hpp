#ifndef RELLU_CANVAS_HPP
#define RELLU_CANVAS_HPP
#include <QRhiWidget>

namespace Rellu {

  /** Renders the design surface using the GPU. */
  class Canvas : public QRhiWidget {
    public:

      /**
       * Constructs a Canvas.
       * @param parent The parent widget.
       */
      explicit Canvas(QWidget* parent = nullptr);

    protected:
      void render(QRhiCommandBuffer* buffer) override;
  };
}

#endif
