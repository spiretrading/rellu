#ifndef GESSO_CANVAS_HPP
#define GESSO_CANVAS_HPP
#include <QRhiWidget>

namespace Gesso {

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
