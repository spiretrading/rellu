#ifndef RELLU_UI_COLOR_PICKER_HPP
#define RELLU_UI_COLOR_PICKER_HPP
#include <functional>
#include <vector>
#include <QComboBox>
#include <QFrame>
#include <QLineEdit>
#include <QRect>
#include <QWidget>
#include "Rellu/Geometry/Color.hpp"
#include "Rellu/Model/Resources.hpp"
#include "Rellu/Model/Style.hpp"

namespace Rellu {

  /** Lists the ways the color picker shows a color's value. */
  enum class ColorModel : unsigned char {

    /** A hexadecimal #RRGGBB value. */
    HEX,

    /** Red, green and blue components from 0 to 255. */
    RGB,

    /** Hue in degrees, saturation and brightness in percent. */
    HSB
  };

  /**
   * Adobe XD's color picker popup: a fill type dropdown, a gradient bar for
   * gradient fills, a saturation and brightness square, hue and alpha
   * sliders, the value fields of a color model, the opacity, an eyedropper,
   * a button adding the color to the document's assets and the document's
   * color swatches. It edits a Fill, a solid fill when gradients are not
   * offered, reporting every change as the user makes it.
   */
  class ColorPicker : public QFrame {
    public:

      /**
       * The type of function called as the user changes the fill.
       * @param fill The fill.
       */
      using ChangeHandler = std::function<void (const Fill& fill)>;

      /**
       * The type of function called when the user adds the fill to the
       * document's assets.
       * @param fill The fill.
       */
      using AddHandler = std::function<void (const Fill& fill)>;

      /** The type of function called when the picker closes. */
      using CloseHandler = std::function<void ()>;

      /**
       * Constructs a ColorPicker editing a black solid fill.
       * @param parent The parent widget.
       */
      explicit ColorPicker(QWidget* parent = nullptr);

      /** Returns the fill edited. */
      const Fill& get_fill() const;

      /**
       * Sets the fill edited without reporting a change.
       * @param fill The fill.
       */
      void set_fill(const Fill& fill);

      /** Returns the color being edited: the solid color or selected stop. */
      Color get_color() const;

      /**
       * Changes the color being edited as the user would, reporting the
       * change.
       * @param color The color.
       */
      void apply_color(const Color& color);

      /**
       * Changes the type of fill as the user would with the dropdown,
       * reporting the change.
       * @param index The index among Solid Color, Linear Gradient and Radial
       *        Gradient.
       */
      void apply_type(int index);

      /** Returns whether gradient fills are offered. */
      bool is_gradient_enabled() const;

      /**
       * Sets whether gradient fills are offered, the fill type dropdown
       * being shown only when they are.
       * @param is_enabled Whether gradients are offered.
       */
      void set_gradient_enabled(bool is_enabled);

      /** Returns the index of the gradient stop being edited. */
      int get_stop() const;

      /**
       * Selects the gradient stop being edited.
       * @param index The stop's index.
       */
      void set_stop(int index);

      /** Returns the color model of the value fields. */
      ColorModel get_model() const;

      /**
       * Sets the color model of the value fields.
       * @param model The color model.
       */
      void set_model(ColorModel model);

      /**
       * Sets the document's swatches listed after the add button.
       * @param swatches The swatches.
       */
      void set_swatches(std::vector<ColorSwatch> swatches);

      /**
       * Sets the function called as the user changes the fill.
       * @param handler The function.
       */
      void set_change_handler(ChangeHandler handler);

      /**
       * Sets the function called when the user adds the fill to the assets,
       * the add button being enabled only while there is one.
       * @param handler The function.
       */
      void set_add_handler(AddHandler handler);

      /**
       * Sets the function called when the picker closes.
       * @param handler The function.
       */
      void set_close_handler(CloseHandler handler);

      /**
       * Shows the picker as a popup to the left of a rectangle, vertically
       * centered on it as far as the screen allows.
       * @param anchor The rectangle in global coordinates.
       */
      void popup(const QRect& anchor);

    protected:
      void hideEvent(QHideEvent* event) override;
      void keyPressEvent(QKeyEvent* event) override;

    private:
      class SaturationSquare;
      class ChannelSlider;
      class GradientBar;
      Fill m_fill;
      int m_stop;
      double m_hue;
      double m_saturation;
      double m_value;
      double m_alpha;
      bool m_is_gradient_enabled;
      bool m_is_picking;
      bool m_is_updating;
      ColorModel m_model;
      std::vector<ColorSwatch> m_swatches;
      ChangeHandler m_change_handler;
      AddHandler m_add_handler;
      CloseHandler m_close_handler;
      QComboBox* m_type;
      GradientBar* m_bar;
      SaturationSquare* m_square;
      ChannelSlider* m_hue_slider;
      ChannelSlider* m_alpha_slider;
      QComboBox* m_model_box;
      QLineEdit* m_hex;
      std::vector<QLineEdit*> m_components;
      QLineEdit* m_opacity;
      QWidget* m_swatch_row;
      QWidget* m_add;

      void load_color();
      void set_hsv(double hue, double saturation, double value, double alpha);
      void store_color();
      void write_color(const Color& color);
      void update_fields();
      void update_type();
      void rebuild_swatches();
      void report();
      void commit_fields();
      void pick_from_screen();
  };

  /**
   * Lets the user click anywhere on the screens to sample a pixel's color,
   * Escape cancelling.
   * @param handler The function called with the color sampled, or not at
   *        all if the user cancels.
   * @param cancel The function called if the user cancels.
   */
  void pick_screen_color(std::function<void (const Color& color)> handler,
    std::function<void ()> cancel = {});
}

#endif
