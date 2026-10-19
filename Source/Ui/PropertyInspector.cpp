#include <algorithm>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Colors.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto LARGE_STEP = 10.0;
  constexpr auto SIDE_MARGIN = 16;
  constexpr auto ICON_BUTTON_SIZE = 30;
  constexpr auto ICON_SIZE = 20;
  constexpr auto SEGMENT_SIZE = 32;
  constexpr auto SWATCH_WIDTH = 30;
  constexpr auto SWATCH_HEIGHT = 18;
  const auto EM_DASH = QString(QChar(0x2014));
  const auto DEGREE = QString(QChar(0x00B0));
  const auto BLEND_SEPARATORS = QStringList(
    {"Normal", "Color Burn", "Color Dodge", "Hard Light", "Exclusion"});

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  NodeId get_source(const Document& document, const NodeId& id) {
    if(auto resolution = resolve_reference(document.get_nodes(), id)) {
      return resolution->m_source;
    }
    return NodeId();
  }

  bool is_structural(const std::string& path) {
    return path == "geometry.radii" || path == "stroke.enabled" ||
      path == "grid.visible" || path == "grid.type" ||
      path == "grid.linked_margins" || path == "layout.scrolling" ||
      path == "effects.inner_shadow" || path == "effects.drop_shadow" ||
      path == "effects.blur";
  }

  bool is_same_structure(const std::vector<PropertyDescriptor>& left,
      const std::vector<PropertyDescriptor>& right) {
    if(left.size() != right.size()) {
      return false;
    }
    for(auto i = std::size_t(0); i != left.size(); ++i) {
      if(left[i].m_path != right[i].m_path ||
          left[i].m_label != right[i].m_label ||
          left[i].m_value.index() != right[i].m_value.index() ||
          left[i].m_is_read_only != right[i].m_is_read_only ||
          (is_structural(left[i].m_path) &&
            !(left[i].m_value == right[i].m_value)) ||
          !is_same_structure(left[i].m_children, right[i].m_children)) {
        return false;
      }
    }
    return true;
  }

  std::optional<Color> parse_color(QString text) {
    text = text.trimmed();
    auto alpha = 1.0;
    if(auto space = text.indexOf(' '); space != -1) {
      auto percent = text.mid(space + 1).trimmed();
      if(percent.endsWith('%')) {
        percent.chop(1);
      }
      auto is_valid = false;
      alpha = percent.toDouble(&is_valid) / 100;
      if(!is_valid || !(alpha >= 0 && alpha <= 1)) {
        return std::nullopt;
      }
      text = text.left(space);
    }
    if(text.startsWith('#')) {
      text.remove(0, 1);
    }
    if(text.size() == 3) {
      text = QString("%1%1%2%2%3%3").arg(text[0]).arg(text[1]).arg(text[2]);
    }
    if(text.size() != 6) {
      return std::nullopt;
    }
    auto is_valid = false;
    auto rgb = text.toUInt(&is_valid, 16);
    if(!is_valid) {
      return std::nullopt;
    }
    return make_color(static_cast<int>((rgb >> 16) & 0xFF),
      static_cast<int>((rgb >> 8) & 0xFF), static_cast<int>(rgb & 0xFF), alpha);
  }

  class FieldLine : public QLineEdit {
    public:
      std::function<void (int steps)> m_step;
      std::function<void ()> m_leave;

    protected:
      void keyPressEvent(QKeyEvent* event) override {
        if(m_step && !isReadOnly() &&
            (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
          auto steps = event->key() == Qt::Key_Up ? 1 : -1;
          if(event->modifiers() & Qt::ShiftModifier) {
            steps *= static_cast<int>(LARGE_STEP);
          }
          m_step(steps);
          event->accept();
          return;
        }
        QLineEdit::keyPressEvent(event);
      }

      void focusOutEvent(QFocusEvent* event) override {
        QLineEdit::focusOutEvent(event);
        if(m_leave) {
          m_leave();
        }
      }
  };

  class Switch : public QAbstractButton {
    public:
      Switch() {
        setCheckable(true);
        setFocusPolicy(Qt::NoFocus);
        setFixedSize(32, 18);
      }

    protected:
      void paintEvent(QPaintEvent* event) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto color = isEnabled() ? QColor(0x4B, 0x4B, 0x4B) :
          QColor(0xC8, 0xC8, 0xC8);
        auto track = QRectF(1.5, 2.5, width() - 3, height() - 5);
        auto radius = track.height() / 2;
        if(isChecked()) {
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawRoundedRect(track, radius, radius);
          painter.setBrush(Colors::PANEL);
          painter.drawEllipse(QPointF(track.right() - radius,
            track.center().y()), radius - 2.5, radius - 2.5);
        } else {
          painter.setPen(QPen(color, 1.5));
          painter.setBrush(Qt::NoBrush);
          painter.drawRoundedRect(track, radius, radius);
          painter.setPen(Qt::NoPen);
          painter.setBrush(color);
          painter.drawEllipse(QPointF(track.left() + radius,
            track.center().y()), radius + 0.5, radius + 0.5);
          painter.setBrush(Colors::PANEL);
          painter.drawEllipse(QPointF(track.left() + radius,
            track.center().y()), radius - 1.5, radius - 1.5);
        }
      }
  };

  class ChoiceBox : public QComboBox {
    protected:
      void paintEvent(QPaintEvent* event) override {
        QComboBox::paintEvent(event);
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto color = isEnabled() ? Colors::ICON : Colors::DISABLED_TEXT;
        auto center = QPointF(width() - 14, height() / 2.0);
        auto pen = QPen(color, 1.3);
        painter.setPen(pen);
        painter.drawPolyline(QList<QPointF>({
          QPointF(center.x() - 4, center.y() - 2),
          QPointF(center.x(), center.y() + 2),
          QPointF(center.x() + 4, center.y() - 2)}));
      }
  };

  struct Display {
    double m_scale;
    QString m_suffix;
  };

  Display get_default_display(const std::string& path) {
    if(path == "appearance.opacity" || path == "geometry.star_ratio" ||
        path == "effects.blur.opacity") {
      return Display(100, "%");
    } else if(path == "effects.inner_shadow.blur" ||
        path == "effects.drop_shadow.blur") {
      return Display(2, "");
    } else if(path == "transform.rotation") {
      return Display(1, DEGREE);
    }
    return Display(1, "");
  }

  Display get_display(const std::string& path, const PropertyEditor* editor) {
    auto display = get_default_display(path);
    if(editor) {
      display.m_scale = editor->m_scale;
      if(!editor->m_suffix.isEmpty()) {
        display.m_suffix = editor->m_suffix;
      }
    }
    return display;
  }

  using Updater = std::function<void (const PropertyDescriptor&)>;

  struct Binding {
    std::function<const PropertyDescriptor* (std::string_view)> m_find;
    std::function<const PropertyEditor* (const PropertyDescriptor&)> m_editor;
    std::function<void (const std::string&, const PropertyValue&)> m_commit;
    std::function<void (const std::string&)> m_finish;
    std::function<void (const std::string&, QWidget*, bool, Updater)>
      m_register;
    ActionRegistry* m_actions;
    bool m_is_enabled;
  };

  QLabel* make_label(const QString& text, const char* name = "field_label") {
    auto label = new QLabel(text);
    label->setObjectName(name);
    return label;
  }

  QLabel* make_icon_label(Glyph glyph) {
    auto label = new QLabel();
    label->setPixmap(get_icon(glyph).pixmap(QSize(18, 18)));
    label->setFixedSize(20, 20);
    return label;
  }

  QWidget* make_row(QHBoxLayout*& layout, int spacing = 8) {
    auto row = new QWidget();
    layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(spacing);
    return row;
  }

  QFrame* make_rule(Qt::Orientation orientation) {
    auto rule = new QFrame();
    rule->setObjectName("section_rule");
    if(orientation == Qt::Horizontal) {
      rule->setFixedHeight(1);
    } else {
      rule->setFixedWidth(1);
    }
    return rule;
  }

  QToolButton* make_icon_button(Glyph glyph, const QString& tip,
      const char* name = "icon_toggle") {
    auto button = new QToolButton();
    button->setObjectName(name);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setIcon(get_icon(glyph));
    button->setIconSize(QSize(ICON_SIZE, ICON_SIZE));
    button->setFixedSize(ICON_BUTTON_SIZE, ICON_BUTTON_SIZE);
    button->setToolTip(tip);
    button->setAccessibleName(tip);
    return button;
  }

  void bind_action(
      const Binding& binding, QToolButton& button, const char* name) {
    auto action =
      binding.m_actions ? binding.m_actions->find(name) : nullptr;
    if(!action || !binding.m_is_enabled) {
      button.setEnabled(false);
      return;
    }
    button.setEnabled(action->isEnabled());
    QObject::connect(action, &QAction::changed, &button, [&button, action] {
      button.setEnabled(action->isEnabled());
    });
    QObject::connect(&button, &QToolButton::clicked, action, [action] {
      action->trigger();
    });
  }

  QToolButton* make_action_button(const Binding& binding, Glyph glyph,
      const QString& tip, const char* action) {
    auto button = make_icon_button(glyph, tip);
    bind_action(binding, *button, action);
    return button;
  }

  QToolButton* make_text_button(const Binding& binding, const QString& text,
      const char* action, std::optional<Glyph> glyph = std::nullopt) {
    auto button = new QToolButton();
    button->setObjectName("text_button");
    button->setFocusPolicy(Qt::NoFocus);
    button->setText(text);
    if(glyph) {
      button->setIcon(get_icon(*glyph));
      button->setIconSize(QSize(18, 18));
      button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    }
    if(action) {
      bind_action(binding, *button, action);
    } else {
      button->setEnabled(false);
    }
    return button;
  }

  struct SectionFrame {
    QWidget* m_widget;
    QVBoxLayout* m_layout;
    QHBoxLayout* m_header;
  };

  SectionFrame make_section(const QString& title, bool has_info) {
    auto widget = new QWidget();
    widget->setObjectName("inspector_section");
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(SIDE_MARGIN, 14, SIDE_MARGIN, 16);
    layout->setSpacing(12);
    auto header = static_cast<QHBoxLayout*>(nullptr);
    if(!title.isEmpty()) {
      auto row = make_row(header, 6);
      header->addWidget(make_label(title.toUpper(), "section_heading"));
      if(has_info) {
        auto info = new QLabel();
        info->setPixmap(get_icon(Glyph::INFO).pixmap(QSize(14, 14)));
        info->setToolTip(title);
        header->addWidget(info);
      }
      header->addStretch(1);
      row->setFixedHeight(22);
      layout->addWidget(row);
    }
    return SectionFrame(widget, layout, header);
  }

  QWidget* make_number(const Binding& binding, const std::string& path,
      const QString& label = QString(), std::optional<Glyph> glyph =
      std::nullopt, int width = 0) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto display = get_display(path, editor);
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto container = make_row(layout, 6);
    if(glyph) {
      layout->addWidget(make_icon_label(*glyph));
    } else if(!label.isEmpty()) {
      layout->addWidget(make_label(label));
    }
    auto line = new FieldLine();
    line->setObjectName("number_field");
    line->setAccessibleName(property->m_label);
    line->setEnabled(binding.m_is_enabled);
    line->setReadOnly(!editor);
    if(width > 0) {
      line->setFixedWidth(width);
    }
    layout->addWidget(line, 1);
    auto is_integer = std::holds_alternative<int>(property->m_value);
    auto shown = std::make_shared<QString>();
    auto scale = display.m_scale;
    auto suffix = display.m_suffix;
    auto minimum = editor ? editor->m_minimum : std::nullopt;
    auto maximum = editor ? editor->m_maximum : std::nullopt;
    auto parse = [=] (QString text) -> std::optional<double> {
      text = text.trimmed();
      if(!suffix.isEmpty() && text.endsWith(suffix)) {
        text.chop(suffix.size());
      }
      auto is_valid = false;
      auto number = text.trimmed().toDouble(&is_valid);
      if(!is_valid || !std::isfinite(number)) {
        return std::nullopt;
      }
      number /= scale;
      if(minimum) {
        number = std::max(number, *minimum);
      }
      if(maximum) {
        number = std::min(number, *maximum);
      }
      return number;
    };
    auto commit = [=, commit = binding.m_commit] (double number) {
      if(is_integer) {
        commit(path, static_cast<int>(std::lround(number)));
      } else {
        commit(path, number);
      }
    };
    if(editor) {
      QObject::connect(line, &QLineEdit::editingFinished, line, [=] {
        if(line->text() == *shown) {
          return;
        }
        if(auto number = parse(line->text())) {
          commit(*number);
        } else {
          line->setText(*shown);
        }
      });
      line->m_step = [=] (int steps) {
        auto base = parse(line->text()).value_or(0);
        if(auto number = parse(QString::number(base * scale + steps))) {
          commit(*number);
        }
      };
      line->m_leave = [=, finish = binding.m_finish] {
        finish(path);
      };
    }
    binding.m_register(path, line, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        auto text = QString();
        if(property.m_is_mixed) {
          line->setPlaceholderText(EM_DASH);
        } else if(auto number = std::get_if<double>(&property.m_value)) {
          line->setPlaceholderText(QString());
          text = to_text(*number * scale) + suffix;
        } else if(auto number = std::get_if<int>(&property.m_value)) {
          line->setPlaceholderText(QString());
          text = to_text(*number) + suffix;
        }
        *shown = text;
        if(line->text() != text) {
          line->setText(text);
        }
      });
    return container;
  }

  QWidget* make_text_field(const Binding& binding, const std::string& path,
      const QString& placeholder) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto line = new FieldLine();
    line->setObjectName("text_field");
    line->setAccessibleName(property->m_label);
    line->setPlaceholderText(placeholder);
    line->setEnabled(binding.m_is_enabled);
    line->setReadOnly(!editor);
    if(editor) {
      QObject::connect(line, &QLineEdit::textEdited, line,
        [=, commit = binding.m_commit] (const QString& text) {
          commit(path, text);
        });
      QObject::connect(line, &QLineEdit::editingFinished, line,
        [=, finish = binding.m_finish] {
          finish(path);
        });
    }
    binding.m_register(path, line, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        auto text = property.m_is_mixed ? QString() : to_text(property.m_value);
        line->setPlaceholderText(property.m_is_mixed ? EM_DASH : placeholder);
        if(line->text() != text) {
          line->setText(text);
        }
      });
    return line;
  }

  QCheckBox* make_check(const Binding& binding, const std::string& path,
      const QString& text = QString()) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto box = new QCheckBox(text);
    box->setFocusPolicy(Qt::NoFocus);
    box->setAccessibleName(property->m_label);
    box->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(box, &QCheckBox::clicked, box,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          auto is_checked = box->checkState() != Qt::Unchecked;
          box->setTristate(false);
          commit(path, is_checked);
          finish(path);
        });
    }
    binding.m_register(path, box, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        if(property.m_is_mixed) {
          box->setTristate(true);
          box->setCheckState(Qt::PartiallyChecked);
        } else if(auto value = std::get_if<bool>(&property.m_value)) {
          box->setTristate(false);
          box->setChecked(*value);
        }
      });
    return box;
  }

  QWidget* make_switch(const Binding& binding, const std::string& path) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto toggle = new Switch();
    toggle->setAccessibleName(property->m_label);
    toggle->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(toggle, &QAbstractButton::clicked, toggle,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          commit(path, toggle->isChecked());
          finish(path);
        });
    }
    binding.m_register(path, toggle, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        if(auto value = std::get_if<bool>(&property.m_value)) {
          toggle->setChecked(*value && !property.m_is_mixed);
        }
      });
    return toggle;
  }

  QComboBox* make_combo(const Binding& binding, const std::string& path,
      const char* name = "choice_field") {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto box = new ChoiceBox();
    box->setObjectName(name);
    box->setFocusPolicy(Qt::NoFocus);
    box->setAccessibleName(property->m_label);
    box->setPlaceholderText(EM_DASH);
    auto options = QStringList();
    if(auto choice = std::get_if<Choice>(&property->m_value)) {
      options = choice->m_options;
    } else {
      options.push_back(to_text(property->m_value));
    }
    for(auto i = 0; i != options.size(); ++i) {
      box->addItem(options[i], i);
      if(path.ends_with("blend_mode") &&
          BLEND_SEPARATORS.contains(options[i])) {
        box->insertSeparator(box->count());
      }
    }
    box->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(box, &QComboBox::activated, box,
        [=, commit = binding.m_commit, finish = binding.m_finish] (
            int index) {
          auto data = box->itemData(index);
          if(!data.isValid()) {
            return;
          }
          commit(path, Choice(data.toInt(), options));
          finish(path);
        });
    }
    binding.m_register(path, box, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        auto index = -1;
        if(property.m_is_mixed) {
          index = -1;
        } else if(auto choice = std::get_if<Choice>(&property.m_value)) {
          index = box->findData(choice->m_index);
        } else {
          box->setItemText(0, to_text(property.m_value));
          index = 0;
        }
        if(box->currentIndex() != index) {
          box->setCurrentIndex(index);
        }
      });
    return box;
  }

  struct Segment {
    int m_index;
    Glyph m_glyph;
    QString m_tip;
  };

  QWidget* make_segments(const Binding& binding, const std::string& path,
      std::vector<Segment> segments, bool is_toggle = false) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto container = make_row(layout, 0);
    auto buttons = std::make_shared<std::vector<QToolButton*>>();
    auto options = QStringList();
    if(auto choice = std::get_if<Choice>(&property->m_value)) {
      options = choice->m_options;
    }
    auto current = std::make_shared<int>(-1);
    auto is_bool = std::holds_alternative<bool>(property->m_value);
    for(auto& segment : segments) {
      auto button = make_icon_button(
        segment.m_glyph, segment.m_tip, is_toggle ? "icon_toggle" : "segment");
      button->setCheckable(true);
      if(!is_toggle) {
        button->setFixedSize(SEGMENT_SIZE + 6, SEGMENT_SIZE);
      }
      button->setEnabled(binding.m_is_enabled && editor);
      layout->addWidget(button);
      buttons->push_back(button);
      auto index = segment.m_index;
      if(editor) {
        QObject::connect(button, &QToolButton::clicked, button,
          [=, commit = binding.m_commit, finish = binding.m_finish] {
            auto chosen = index;
            if(is_toggle && *current == index) {
              chosen = 0;
            }
            if(is_bool) {
              commit(path, chosen != 0);
            } else {
              commit(path, Choice(chosen, options));
            }
            finish(path);
          });
      }
    }
    if(is_toggle) {
      layout->setSpacing(4);
    }
    binding.m_register(path, container, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        auto index = -1;
        if(property.m_is_mixed) {
          index = -1;
        } else if(auto choice = std::get_if<Choice>(&property.m_value)) {
          index = choice->m_index;
        } else if(auto value = std::get_if<bool>(&property.m_value)) {
          index = *value ? 1 : 0;
        }
        *current = index;
        for(auto i = std::size_t(0); i != buttons->size(); ++i) {
          (*buttons)[i]->setChecked(segments[i].m_index == index);
        }
      });
    return container;
  }

  void paint_swatch(QToolButton& button, const PropertyDescriptor* color,
      const PropertyDescriptor* section) {
    auto ratio = button.devicePixelRatioF();
    auto pixmap = QPixmap(static_cast<int>(SWATCH_WIDTH * ratio),
      static_cast<int>(SWATCH_HEIGHT * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto bounds = QRectF(0.5, 0.5, SWATCH_WIDTH - 1, SWATCH_HEIGHT - 1);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(bounds, 2, 2);
    painter.setBrush(QColor(0xD0, 0xD0, 0xD0));
    for(auto y = 0; y < SWATCH_HEIGHT; y += 6) {
      for(auto x = (y / 6) % 2 * 6; x < SWATCH_WIDTH; x += 12) {
        painter.drawRect(QRectF(x, y, 6, 6).intersected(bounds));
      }
    }
    auto stops = section ? find_property(
      section->m_children, section->m_path + ".stops") : nullptr;
    auto image = section ? find_property(
      section->m_children, section->m_path + ".image") : nullptr;
    if(color && !color->m_is_mixed) {
      if(auto value = std::get_if<Color>(&color->m_value)) {
        painter.setBrush(to_qcolor(*value));
        painter.drawRoundedRect(bounds, 2, 2);
      }
    } else if(stops && !stops->m_children.empty()) {
      auto gradient = QLinearGradient(bounds.topLeft(), bounds.topRight());
      for(auto i = std::size_t(0); i != stops->m_children.size(); ++i) {
        auto& stop = stops->m_children[i];
        if(auto value = std::get_if<Color>(&stop.m_value)) {
          gradient.setColorAt(static_cast<double>(i) /
            std::max<std::size_t>(stops->m_children.size() - 1, 1),
            to_qcolor(*value));
        }
      }
      painter.setBrush(gradient);
      painter.drawRoundedRect(bounds, 2, 2);
    } else if(image) {
      painter.setBrush(QColor(0x9A, 0xB8, 0xD6));
      painter.drawRoundedRect(bounds, 2, 2);
      painter.setBrush(QColor(0x5C, 0x8A, 0x5C));
      painter.drawPolygon(QList<QPointF>({QPointF(4, SWATCH_HEIGHT - 3),
        QPointF(12, 6), QPointF(20, SWATCH_HEIGHT - 3)}));
    }
    painter.setPen(QPen(Colors::BORDER, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(bounds, 2, 2);
    painter.end();
    button.setIcon(QIcon(pixmap));
  }

  QToolButton* make_swatch(const Binding& binding,
      const std::string& color_path, const std::string& section_path) {
    auto button = new QToolButton();
    button->setObjectName("swatch");
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setIconSize(QSize(SWATCH_WIDTH, SWATCH_HEIGHT));
    button->setFixedSize(SWATCH_WIDTH + 4, SWATCH_HEIGHT + 4);
    auto color = binding.m_find(color_path);
    auto editor = color ? binding.m_editor(*color) : nullptr;
    button->setEnabled(binding.m_is_enabled);
    auto value = std::make_shared<Color>();
    if(editor) {
      QObject::connect(button, &QToolButton::clicked, button,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          auto chosen = QColorDialog::getColor(to_qcolor(*value), button,
            "Color", QColorDialog::ShowAlphaChannel);
          if(chosen.isValid()) {
            commit(color_path, Color(chosen.redF(), chosen.greenF(),
              chosen.blueF(), chosen.alphaF()));
            finish(color_path);
          }
        });
    }
    auto update = [=] (const PropertyDescriptor& property) {
      if(property.m_path == color_path) {
        if(auto color_value = std::get_if<Color>(&property.m_value)) {
          *value = *color_value;
        }
        paint_swatch(*button, &property, nullptr);
      } else {
        paint_swatch(*button,
          find_property(property.m_children, color_path), &property);
      }
    };
    if(color) {
      binding.m_register(color_path, button, static_cast<bool>(editor),
        update);
    } else if(!section_path.empty() && binding.m_find(section_path)) {
      binding.m_register(section_path, button, false, update);
    } else {
      paint_swatch(*button, nullptr, nullptr);
    }
    return button;
  }

  QWidget* make_style_row(const Binding& binding, const std::string& path,
      const QString& label, bool has_eyedropper,
      const std::string& color_path, const std::string& section_path) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 14);
    if(auto box = make_check(binding, path)) {
      layout->addWidget(box);
    }
    layout->addWidget(make_swatch(binding, color_path, section_path));
    layout->addWidget(make_label(label, "row_label"), 1);
    if(has_eyedropper) {
      auto dropper =
        make_icon_button(Glyph::EYEDROPPER, "Pick color from screen");
      dropper->setEnabled(false);
      layout->addWidget(dropper);
    }
    return row;
  }

  void build_align(const Binding& binding, QVBoxLayout& layout) {
    auto row_layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(row_layout, 2);
    auto add = [&] (Glyph glyph, const QString& tip, const char* action) {
      row_layout->addWidget(make_action_button(binding, glyph, tip, action));
    };
    add(Glyph::ALIGN_TOP, "Align Top", "object.align.top");
    add(Glyph::ALIGN_MIDDLE, "Align Middle (Vertically)",
      "object.align.middle");
    add(Glyph::ALIGN_BOTTOM, "Align Bottom", "object.align.bottom");
    add(Glyph::DISTRIBUTE_HORIZONTALLY, "Distribute Horizontally",
      "object.distribute.horizontally");
    row_layout->addSpacing(4);
    row_layout->addWidget(make_rule(Qt::Vertical));
    row_layout->addSpacing(4);
    add(Glyph::ALIGN_LEFT, "Align Left", "object.align.left");
    add(Glyph::ALIGN_CENTER, "Align Center (Horizontally)",
      "object.align.center");
    add(Glyph::ALIGN_RIGHT, "Align Right", "object.align.right");
    add(Glyph::DISTRIBUTE_VERTICALLY, "Distribute Vertically",
      "object.distribute.vertically");
    layout.addWidget(row);
  }

  void build_boolean(const Binding& binding, QVBoxLayout& layout) {
    auto row_layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(row_layout, 2);
    auto is_grid = static_cast<bool>(binding.m_find("layout.repeat_grid"));
    auto grid_button = make_text_button(binding, "Repeat Grid",
      is_grid ? "object.ungroup" : "object.repeat_grid", Glyph::REPEAT_GRID);
    grid_button->setCheckable(true);
    grid_button->setChecked(is_grid);
    row_layout->addWidget(grid_button);
    row_layout->addStretch(1);
    auto buttons = std::make_shared<std::vector<QToolButton*>>();
    for(auto [glyph, tip, action] : {
        std::tuple(Glyph::BOOLEAN_ADD, "Add", "object.path.add"),
        std::tuple(Glyph::BOOLEAN_SUBTRACT, "Subtract", "object.path.subtract"),
        std::tuple(
          Glyph::BOOLEAN_INTERSECT, "Intersect", "object.path.intersect"),
        std::tuple(Glyph::BOOLEAN_EXCLUDE, "Exclude Overlap",
          "object.path.exclude")}) {
      auto button = make_action_button(binding, glyph, tip, action);
      button->setCheckable(true);
      row_layout->addWidget(button);
      buttons->push_back(button);
    }
    if(binding.m_find("boolean.operation")) {
      binding.m_register("boolean.operation", row, false,
        [=] (const PropertyDescriptor& property) {
          auto choice = std::get_if<Choice>(&property.m_value);
          for(auto i = std::size_t(0); i != buttons->size(); ++i) {
            (*buttons)[i]->setChecked(choice && !property.m_is_mixed &&
              choice->m_index == static_cast<int>(i));
          }
        });
    }
    layout.addWidget(row);
  }

  void build_component(const Binding& binding, SectionFrame& frame) {
    if(!binding.m_find("component.name")) {
      frame.m_header->addWidget(make_action_button(
        binding, Glyph::PLUS, "Make Component (Ctrl + K)",
        "object.make_component"));
      return;
    }
    auto is_main = static_cast<bool>(binding.m_find("component.main"));
    if(is_main) {
      auto add = make_icon_button(Glyph::PLUS, "Add State");
      auto menu = new QMenu(add);
      for(auto name : {"component.add_state", "component.add_hover_state",
          "component.add_toggle_state"}) {
        if(auto action =
            binding.m_actions ? binding.m_actions->find(name) : nullptr) {
          menu->addAction(action);
        }
      }
      add->setMenu(menu);
      add->setPopupMode(QToolButton::InstantPopup);
      add->setEnabled(binding.m_is_enabled && !menu->isEmpty() &&
        !binding.m_find("component.linked"));
      frame.m_header->addWidget(add);
    }
    if(auto name = make_text_field(binding, "component.name", QString())) {
      frame.m_layout->addWidget(name);
    }
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout);
    if(auto state = make_combo(binding, "component.state")) {
      layout->addWidget(state, 1);
    }
    frame.m_layout->addWidget(row);
    if(auto state_name =
        make_text_field(binding, "component.state_name", QString())) {
      frame.m_layout->addWidget(state_name);
      frame.m_layout->addWidget(make_text_button(
        binding, "Delete State", "component.delete_state"));
    }
    if(!is_main) {
      frame.m_layout->addWidget(make_text_button(
        binding, "Edit Main Component", "object.edit_main_component"));
    }
  }

  void build_transform(const Binding& binding, SectionFrame& frame) {
    auto is_artboard =
      static_cast<bool>(binding.m_find("transform.orientation"));
    if(!is_artboard && binding.m_find("transform.rotation")) {
      frame.m_header->addWidget(make_action_button(
        binding, Glyph::CUBE, "3D Transforms", "view.transforms_3d"));
    }
    auto grid = new QWidget();
    auto layout = new QGridLayout(grid);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(10);
    layout->setVerticalSpacing(14);
    auto place = [&] (QWidget* widget, int row, int column) {
      if(widget) {
        layout->addWidget(widget, row, column);
      }
    };
    place(make_number(binding, "transform.width", "W"), 0, 0);
    place(make_number(binding, "transform.height", "H"), 1, 0);
    if(binding.m_find("transform.lock_aspect")) {
      auto lock = make_icon_button(Glyph::LOCK_ASPECT, "Lock Aspect");
      lock->setFixedSize(16, 36);
      lock->setIconSize(QSize(12, 12));
      lock->setEnabled(false);
      layout->addWidget(lock, 0, 1, 2, 1, Qt::AlignCenter);
    }
    place(make_number(binding, "transform.x", "X"), 0, 2);
    place(make_number(binding, "transform.y", "Y"), 1, 2);
    if(is_artboard) {
      place(make_segments(binding, "transform.orientation", {
        Segment(0, Glyph::PORTRAIT, "Portrait"),
        Segment(1, Glyph::LANDSCAPE, "Landscape")}), 0, 3);
    } else {
      place(make_number(binding, "transform.rotation", QString(),
        Glyph::ROTATION), 0, 3);
    }
    layout->setColumnStretch(0, 3);
    layout->setColumnStretch(2, 3);
    layout->setColumnStretch(3, 3);
    frame.m_layout->addWidget(grid);
    if(binding.m_find("transform.scroll")) {
      auto row_layout = static_cast<QHBoxLayout*>(nullptr);
      auto row = make_row(row_layout, 2);
      row_layout->addWidget(make_action_button(binding,
        Glyph::FLIP_HORIZONTAL, "Flip Horizontal",
        "object.transform.flip_horizontally"));
      row_layout->addWidget(make_action_button(binding, Glyph::FLIP_VERTICAL,
        "Flip Vertical", "object.transform.flip_vertically"));
      row_layout->addSpacing(20);
      auto buttons = std::make_shared<std::vector<QToolButton*>>();
      for(auto [glyph, tip, action] : {
          std::tuple(Glyph::SCROLL_HORIZONTAL, "Horizontal Scroll",
            "object.scroll_group.horizontal"),
          std::tuple(Glyph::SCROLL_VERTICAL, "Vertical Scroll",
            "object.scroll_group.vertical"),
          std::tuple(Glyph::SCROLL_PANNING, "Horizontal & Vertical Scroll",
            "object.scroll_group.panning")}) {
        auto button = make_action_button(binding, glyph, tip, action);
        button->setCheckable(true);
        row_layout->addWidget(button);
        buttons->push_back(button);
      }
      row_layout->addStretch(1);
      binding.m_register("transform.scroll", row, false,
        [=] (const PropertyDescriptor& property) {
          auto choice = std::get_if<Choice>(&property.m_value);
          for(auto i = std::size_t(0); i != buttons->size(); ++i) {
            (*buttons)[i]->setChecked(choice && !property.m_is_mixed &&
              choice->m_index == static_cast<int>(i) + 1);
          }
        });
      frame.m_layout->addWidget(row);
    }
    if(auto box = make_check(binding, "transform.fixed_position",
        "Fix Position When Scrolling")) {
      frame.m_layout->addWidget(box);
    }
  }

  void build_layout(const Binding& binding, SectionFrame& frame) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout);
    layout->addWidget(make_label("Responsive Resize", "row_label"), 1);
    if(auto toggle = make_switch(binding, "layout.responsive")) {
      layout->addWidget(toggle);
    }
    frame.m_layout->addWidget(row);
    for(auto [path, label] : {std::pair("layout.padding", "Padding"),
        std::pair("layout.stack", "Stack")}) {
      if(!binding.m_find(path)) {
        continue;
      }
      auto toggle_row = make_row(layout);
      layout->addWidget(make_label(label, "row_label"), 1);
      layout->addWidget(make_switch(binding, path));
      frame.m_layout->addWidget(toggle_row);
      if(std::string_view(path) == "layout.padding" &&
          binding.m_find("layout.padding.top")) {
        auto sides = make_row(layout, 10);
        for(auto [side, letter] : {std::pair("layout.padding.top", "T"),
            std::pair("layout.padding.right", "R"),
            std::pair("layout.padding.bottom", "B"),
            std::pair("layout.padding.left", "L")}) {
          layout->addWidget(make_number(binding, side, letter), 1);
        }
        frame.m_layout->addWidget(sides);
      } else if(std::string_view(path) == "layout.stack" &&
          binding.m_find("layout.stack.orientation")) {
        auto stack = make_row(layout, 14);
        layout->addWidget(make_segments(binding, "layout.stack.orientation", {
          Segment(1, Glyph::DISTRIBUTE_VERTICALLY, "Vertical"),
          Segment(0, Glyph::DISTRIBUTE_HORIZONTALLY, "Horizontal")}));
        layout->addWidget(
          make_number(binding, "layout.stack.spacing", "Spacing"), 1);
        frame.m_layout->addWidget(stack);
      }
    }
    if(binding.m_find("layout.repeat_grid")) {
      for(auto [path, label] : {
          std::pair("layout.repeat_grid.column_spacing", "Column Spacing"),
          std::pair("layout.repeat_grid.row_spacing", "Row Spacing")}) {
        frame.m_layout->addWidget(make_number(binding, path, label));
      }
    }
    if(binding.m_find("layout.scrolling")) {
      frame.m_layout->addWidget(make_label("SCROLLING", "section_heading"));
      frame.m_layout->addWidget(make_combo(binding, "layout.scrolling"));
      if(auto height = make_number(
          binding, "layout.viewport_height", "Viewport Height")) {
        frame.m_layout->addWidget(height);
      }
    }
  }

  void build_text(const Binding& binding, SectionFrame& frame) {
    if(auto family = make_combo(binding, "text.family", "underlined_choice")) {
      frame.m_layout->addWidget(family);
    }
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 14);
    if(auto size = make_number(binding, "text.size", QString(), std::nullopt,
        48)) {
      layout->addWidget(size);
    }
    if(auto style = make_combo(binding, "text.style", "underlined_choice")) {
      layout->addWidget(style, 1);
    }
    frame.m_layout->addWidget(row);
    auto spacing = make_row(layout, 14);
    for(auto [path, glyph] : {
        std::pair("text.letter_spacing", Glyph::CHARACTER_SPACING),
        std::pair("text.line_height", Glyph::LINE_SPACING),
        std::pair("text.paragraph_spacing", Glyph::PARAGRAPH_SPACING)}) {
      if(auto field = make_number(binding, path, QString(), glyph)) {
        layout->addWidget(field, 1);
      }
    }
    frame.m_layout->addWidget(spacing);
    auto alignment = make_row(layout, 14);
    if(auto segments = make_segments(binding, "text.alignment", {
        Segment(0, Glyph::TEXT_LEFT, "Left Align"),
        Segment(1, Glyph::TEXT_CENTER, "Center"),
        Segment(2, Glyph::TEXT_RIGHT, "Right Align")})) {
      layout->addWidget(segments);
    }
    if(auto segments = make_segments(binding, "text.frame", {
        Segment(0, Glyph::AUTO_WIDTH, "Auto Width"),
        Segment(1, Glyph::AUTO_HEIGHT, "Auto Height"),
        Segment(2, Glyph::FIXED_SIZE, "Fixed Size")})) {
      layout->addWidget(segments);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(alignment);
    auto styles = make_row(layout, 4);
    if(auto toggles = make_segments(binding, "text.transform", {
        Segment(1, Glyph::UPPERCASE, "Uppercase"),
        Segment(2, Glyph::LOWERCASE, "Lowercase"),
        Segment(3, Glyph::TITLE_CASE, "Title Case")}, true)) {
      layout->addWidget(toggles);
    }
    if(auto toggles = make_segments(binding, "text.script", {
        Segment(1, Glyph::SUPERSCRIPT, "Superscript"),
        Segment(2, Glyph::SUBSCRIPT, "Subscript")}, true)) {
      layout->addWidget(toggles);
    }
    if(auto toggle = make_segments(binding, "text.underline",
        {Segment(1, Glyph::UNDERLINE, "Underline")}, true)) {
      layout->addWidget(toggle);
    }
    if(auto toggle = make_segments(binding, "text.strikethrough",
        {Segment(1, Glyph::STRIKETHROUGH, "Strikethrough")}, true)) {
      layout->addWidget(toggle);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(styles);
    auto link = make_row(layout, 8);
    layout->addWidget(make_icon_label(Glyph::HYPERLINK));
    if(auto field =
        make_text_field(binding, "text.hyperlink", "Insert URL here")) {
      layout->addWidget(field, 1);
    }
    frame.m_layout->addWidget(link);
  }

  void build_appearance(const Binding& binding, SectionFrame& frame) {
    auto opacity = binding.m_find("appearance.opacity");
    if(opacity) {
      frame.m_layout->addWidget(make_label("Opacity"));
      auto layout = static_cast<QHBoxLayout*>(nullptr);
      auto row = make_row(layout, 16);
      auto slider = new QSlider(Qt::Horizontal);
      slider->setObjectName("opacity_slider");
      slider->setAccessibleName("Opacity");
      slider->setFocusPolicy(Qt::NoFocus);
      slider->setRange(0, 100);
      layout->addWidget(slider, 1);
      auto field = make_number(
        binding, "appearance.opacity", QString(), std::nullopt, 44);
      layout->addWidget(field);
      auto editor = binding.m_editor(*opacity);
      slider->setEnabled(binding.m_is_enabled && editor);
      if(editor) {
        QObject::connect(slider, &QSlider::sliderMoved, slider,
          [commit = binding.m_commit] (int value) {
            commit("appearance.opacity", value / 100.0);
          });
        QObject::connect(slider, &QSlider::sliderReleased, slider,
          [finish = binding.m_finish] {
            finish("appearance.opacity");
          });
      }
      binding.m_register("appearance.opacity", slider, false,
        [=] (const PropertyDescriptor& property) {
          if(auto value = std::get_if<double>(&property.m_value)) {
            if(!slider->isSliderDown()) {
              slider->setValue(static_cast<int>(std::lround(*value * 100)));
            }
          }
        });
      frame.m_layout->addWidget(row);
    }
    if(binding.m_find("appearance.blend_mode")) {
      frame.m_layout->addWidget(make_label("Blend Mode"));
      frame.m_layout->addWidget(make_combo(binding, "appearance.blend_mode"));
    }
  }

  void build_geometry(const Binding& binding, SectionFrame& frame) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 12);
    if(auto radii = binding.m_find("geometry.radii")) {
      layout->addWidget(make_segments(binding, "geometry.radii", {
        Segment(0, Glyph::CORNERS_SAME, "Same radius for all corners"),
        Segment(1, Glyph::CORNERS_DIFFERENT,
          "Different radius for each corner")}));
      auto choice = std::get_if<Choice>(&radii->m_value);
      if(choice && choice->m_index == 0 && !radii->m_is_mixed) {
        layout->addWidget(make_number(binding, "geometry.radii.all"), 1);
      } else {
        for(auto path : {"geometry.radii.top_left", "geometry.radii.top_right",
            "geometry.radii.bottom_right", "geometry.radii.bottom_left"}) {
          layout->addWidget(make_number(binding, path), 1);
        }
      }
    } else {
      if(auto field = make_number(
          binding, "geometry.corners", QString(), Glyph::POLYGON)) {
        layout->addWidget(field, 1);
      }
      if(auto field = make_number(
          binding, "geometry.corner_radius", QString(), Glyph::CORNERS_SAME)) {
        layout->addWidget(field, 1);
      }
      if(auto field = make_number(binding, "geometry.star_ratio", QString(),
          Glyph::LAYER_POLYGON)) {
        layout->addWidget(field, 1);
      }
    }
    frame.m_layout->addWidget(row);
  }

  void build_fill(const Binding& binding, SectionFrame& frame) {
    frame.m_layout->addWidget(
      make_style_row(
        binding, "fill.enabled", "Fill", true, "fill.color", "fill"));
  }

  void build_stroke(const Binding& binding, SectionFrame& frame) {
    frame.m_layout->addWidget(make_style_row(
      binding, "stroke.enabled", "Border", true, "stroke.color", ""));
    if(!binding.m_find("stroke.width")) {
      return;
    }
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto sizes = make_row(layout, 12);
    layout->addSpacing(28);
    for(auto [path, label] : {std::pair("stroke.width", "Size"),
        std::pair("stroke.dash", "Dash"), std::pair("stroke.gap", "Gap")}) {
      if(auto field = make_number(binding, path, label)) {
        layout->addWidget(field, 1);
      }
    }
    frame.m_layout->addWidget(sizes);
    auto ends = make_row(layout, 18);
    layout->addSpacing(28);
    if(auto segments = make_segments(binding, "stroke.alignment", {
        Segment(1, Glyph::STROKE_INNER, "Inner Stroke"),
        Segment(2, Glyph::STROKE_OUTER, "Outer Stroke"),
        Segment(0, Glyph::STROKE_CENTER, "Center Stroke")})) {
      layout->addWidget(segments);
    }
    if(auto segments = make_segments(binding, "stroke.cap", {
        Segment(0, Glyph::CAP_BUTT, "Butt Cap"),
        Segment(1, Glyph::CAP_ROUND, "Round Cap"),
        Segment(2, Glyph::CAP_PROJECTING, "Projecting Cap")})) {
      layout->addWidget(segments);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(ends);
    auto joins = make_row(layout, 18);
    layout->addSpacing(28);
    if(auto segments = make_segments(binding, "stroke.join", {
        Segment(0, Glyph::JOIN_MITER, "Miter Join"),
        Segment(1, Glyph::JOIN_ROUND, "Round Join"),
        Segment(2, Glyph::JOIN_BEVEL, "Bevel Join")})) {
      layout->addWidget(segments);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(joins);
  }

  void build_grid(const Binding& binding, SectionFrame& frame) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 14);
    if(auto box = make_check(binding, "grid.visible")) {
      layout->addWidget(box);
    }
    if(auto type = make_combo(binding, "grid.type")) {
      layout->addWidget(type, 1);
    }
    frame.m_layout->addWidget(row);
    auto visible = binding.m_find("grid.visible");
    if(!visible || !std::get<bool>(visible->m_value)) {
      return;
    }
    if(binding.m_find("grid.columns")) {
      auto columns = make_row(layout, 12);
      layout->addWidget(make_swatch(binding, "grid.column_color", ""));
      layout->addWidget(make_label("Columns", "row_label"), 1);
      layout->addWidget(make_number(
        binding, "grid.columns", QString(), std::nullopt, 40));
      frame.m_layout->addWidget(columns);
      for(auto [path, label] : {std::pair("grid.gutter", "Gutter Width"),
          std::pair("grid.column_width", "Column Width")}) {
        auto field_row = make_row(layout, 12);
        layout->addWidget(make_label(label), 1);
        layout->addWidget(make_number(
          binding, path, QString(), std::nullopt, 40));
        frame.m_layout->addWidget(field_row);
      }
      auto margins = make_row(layout, 12);
      layout->addWidget(make_segments(binding, "grid.linked_margins", {
        Segment(1, Glyph::MARGINS_LINKED, "Linked Margins"),
        Segment(0, Glyph::MARGINS_SEPARATE, "Different Margins")}));
      auto linked = binding.m_find("grid.linked_margins");
      if(linked && std::get<bool>(linked->m_value)) {
        layout->addStretch(1);
        layout->addWidget(make_number(
          binding, "grid.margin_top", QString(), std::nullopt, 40));
      } else {
        for(auto path : {"grid.margin_top", "grid.margin_right",
            "grid.margin_bottom", "grid.margin_left"}) {
          layout->addWidget(make_number(binding, path), 1);
        }
      }
      frame.m_layout->addWidget(margins);
      auto defaults = make_row(layout, 0);
      layout->addWidget(make_text_button(binding, "Use Default", nullptr), 1);
      layout->addWidget(make_text_button(binding, "Make Default", nullptr), 1);
      frame.m_layout->addWidget(defaults);
    } else {
      auto square = make_row(layout, 12);
      layout->addWidget(make_swatch(binding, "grid.color", ""));
      layout->addWidget(make_label("Square Size", "row_label"), 1);
      layout->addWidget(make_number(
        binding, "grid.size", QString(), std::nullopt, 40));
      frame.m_layout->addWidget(square);
    }
  }

  void build_effects(const Binding& binding, SectionFrame& frame) {
    for(auto [path, label] : {
        std::pair(std::string("effects.inner_shadow"), "Inner Shadow"),
        std::pair(std::string("effects.drop_shadow"), "Drop Shadow")}) {
      auto row = binding.m_find(path);
      if(!row) {
        continue;
      }
      frame.m_layout->addWidget(
        make_style_row(binding, path, label, false, path + ".color", ""));
      if(std::get<bool>(row->m_value) && binding.m_find(path + ".x")) {
        auto layout = static_cast<QHBoxLayout*>(nullptr);
        auto offsets = make_row(layout, 12);
        layout->addSpacing(28);
        for(auto [name, text] : {std::pair(".x", "X"), std::pair(".y", "Y"),
            std::pair(".blur", "B")}) {
          layout->addWidget(make_number(binding, path + name, text), 1);
        }
        frame.m_layout->addWidget(offsets);
      }
    }
    if(auto blur = binding.m_find("effects.blur")) {
      auto layout = static_cast<QHBoxLayout*>(nullptr);
      auto row = make_row(layout, 14);
      if(auto box = make_check(binding, "effects.blur")) {
        layout->addWidget(box);
      }
      if(auto type = make_combo(binding, "effects.blur.type")) {
        layout->addWidget(type, 1);
      }
      frame.m_layout->addWidget(row);
      if(std::get<bool>(blur->m_value)) {
        for(auto [path, label] : {
            std::pair("effects.blur.amount", "Blur Amount"),
            std::pair("effects.blur.brightness", "Brightness"),
            std::pair("effects.blur.opacity", "Effect Opacity")}) {
          if(auto field = make_number(binding, path, label)) {
            frame.m_layout->addWidget(field);
          }
        }
      }
    }
  }

  void build_export(const Binding& binding, SectionFrame& frame) {
    if(auto box = make_check(binding, "export.marked", "Mark for Export")) {
      frame.m_layout->addWidget(box);
    }
  }

  void build_generic(const Binding& binding,
      const PropertyDescriptor& property, QVBoxLayout& layout) {
    for(auto& child : property.m_children) {
      auto row_layout = static_cast<QHBoxLayout*>(nullptr);
      auto row = make_row(row_layout, 12);
      row_layout->addWidget(make_label(child.m_label), 1);
      if(std::holds_alternative<double>(child.m_value) ||
          std::holds_alternative<int>(child.m_value)) {
        row_layout->addWidget(make_number(
          binding, child.m_path, QString(), std::nullopt, 60));
      } else if(std::holds_alternative<bool>(child.m_value)) {
        row_layout->addWidget(make_check(binding, child.m_path));
      } else if(std::holds_alternative<Choice>(child.m_value)) {
        row_layout->addWidget(make_combo(binding, child.m_path), 1);
      } else if(std::holds_alternative<Color>(child.m_value)) {
        row_layout->addWidget(make_swatch(binding, child.m_path, ""));
      } else if(!std::holds_alternative<std::monostate>(child.m_value)) {
        row_layout->addWidget(
          make_text_field(binding, child.m_path, QString()), 1);
      }
      layout.addWidget(row);
      build_generic(binding, child, layout);
    }
  }

  bool has_header(const std::string& path) {
    return path != "align" && path != "boolean" && path != "export" &&
      path != "geometry" && path != "fill" && path != "stroke";
  }

  bool has_info(const std::string& path) {
    return path == "component" || path == "transform" || path == "layout";
  }

  bool continues_group(const std::string& path) {
    return path == "geometry" || path == "fill" || path == "stroke";
  }

  std::vector<PropertyDescriptor> make_empty_sections() {
    auto make = [] (std::string path, QString label) {
      return PropertyDescriptor(std::move(path), std::move(label),
        PropertyValue(), std::vector<PropertyDescriptor>());
    };
    auto add = [] (PropertyDescriptor& section, const std::string& name,
        QString label, PropertyValue value) {
      section.m_children.push_back(PropertyDescriptor(
        section.m_path + "." + name, std::move(label), std::move(value),
        std::vector<PropertyDescriptor>()));
    };
    auto sections = std::vector<PropertyDescriptor>();
    sections.push_back(make("align", "Align"));
    sections.push_back(make("boolean", "Boolean"));
    sections.push_back(make("component", "Component"));
    auto transform = make("transform", "Transform");
    add(transform, "width", "W", 0.0);
    add(transform, "height", "H", 0.0);
    add(transform, "lock_aspect", "Lock Aspect", false);
    add(transform, "x", "X", 0.0);
    add(transform, "y", "Y", 0.0);
    sections.push_back(std::move(transform));
    auto layout = make("layout", "Layout");
    add(layout, "responsive", "Responsive Resize", true);
    sections.push_back(std::move(layout));
    auto appearance = make("appearance", "Appearance");
    add(appearance, "opacity", "Opacity", 0.0);
    sections.push_back(std::move(appearance));
    return sections;
  }

  QWidget* make_static_combo(const QString& label, const QString& value) {
    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(make_label(label));
    auto box = new ChoiceBox();
    box->setObjectName("choice_field");
    box->addItem(value);
    box->setEnabled(false);
    layout->addWidget(box);
    return widget;
  }
}

struct PropertyInspector::Field {
  std::string m_path;
  QWidget* m_input;
  bool m_is_editable;
  Updater m_update;
};

PropertyInspector::PropertyInspector(QWidget* parent)
    : QWidget(parent),
      m_document(nullptr),
      m_editor(nullptr),
      m_actions(nullptr),
      m_workspace(Workspace::DESIGN),
      m_measurer(estimate_text_bounds),
      m_generation(0),
      m_is_committing(false),
      m_is_rebuild_pending(false),
      m_scroll(new QScrollArea()),
      m_content(nullptr),
      m_layout(nullptr) {
  setObjectName("inspector");
  m_scroll->setObjectName("inspector_scroll");
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_scroll);
  rebuild();
}

PropertyInspector::~PropertyInspector() = default;

void PropertyInspector::set_document(Document* document) {
  m_selection_subscription.release();
  m_editor = nullptr;
  attach(document);
  rebuild();
}

Editor* PropertyInspector::get_editor() const {
  return m_editor;
}

void PropertyInspector::set_editor(Editor* editor) {
  m_selection_subscription.release();
  m_editor = editor;
  if(!m_editor) {
    attach(nullptr);
    rebuild();
    return;
  }
  attach(&m_editor->get_document());
  auto follow = [this] {
    auto& selection = m_editor->get_selection();
    auto nodes = selection.get_nodes();
    auto primary =
      std::find(nodes.begin(), nodes.end(), selection.get_primary());
    if(primary != nodes.end()) {
      std::rotate(primary, primary + 1, nodes.end());
    }
    set_nodes(std::move(nodes));
  };
  m_selection_subscription = m_editor->get_selection().subscribe(follow);
  follow();
}

void PropertyInspector::set_property_editors(PropertyEditors editors) {
  m_property_editors = std::move(editors);
  rebuild();
}

void PropertyInspector::set_measurer(TextMeasurer measurer) {
  m_measurer = std::move(measurer);
  refresh();
}

void PropertyInspector::set_actions(ActionRegistry* actions) {
  m_actions = actions;
  rebuild();
}

void PropertyInspector::set_workspace(Workspace workspace) {
  m_workspace = workspace;
  rebuild();
}

const NodeId& PropertyInspector::get_current() const {
  return m_current;
}

void PropertyInspector::set_current(const NodeId& id) {
  if(id.is_nil()) {
    set_nodes({});
  } else {
    set_nodes({id});
  }
}

const std::vector<NodeId>& PropertyInspector::get_nodes() const {
  return m_nodes;
}

void PropertyInspector::set_nodes(std::vector<NodeId> nodes) {
  m_nodes = std::move(nodes);
  m_current = m_nodes.empty() ? NodeId() : m_nodes.back();
  m_edited_path.clear();
  refresh();
}

const std::vector<PropertyDescriptor>&
    PropertyInspector::get_properties() const {
  return m_properties;
}

std::vector<std::string> PropertyInspector::get_sections() const {
  auto sections = std::vector<std::string>();
  for(auto& section : m_sections) {
    sections.push_back(section.first);
  }
  return sections;
}

QWidget* PropertyInspector::find_section(std::string_view path) const {
  for(auto& section : m_sections) {
    if(section.first == path) {
      return section.second;
    }
  }
  return nullptr;
}

QWidget* PropertyInspector::find_field(std::string_view path) const {
  for(auto& field : m_fields) {
    if(field->m_path == path && field->m_is_editable) {
      return field->m_input;
    }
  }
  return nullptr;
}

QWidget* PropertyInspector::find_display(std::string_view path) const {
  for(auto& field : m_fields) {
    if(field->m_path == path) {
      return field->m_input;
    }
  }
  return nullptr;
}

void PropertyInspector::attach(Document* document) {
  m_subscription.release();
  m_document = document;
  m_nodes.clear();
  m_current = NodeId();
  m_edited_path.clear();
  if(m_document) {
    m_subscription = m_document->subscribe([this] (const auto& changes) {
      on_change(changes);
    });
  }
}

void PropertyInspector::refresh() {
  auto properties = std::vector<PropertyDescriptor>();
  m_sources.clear();
  if(m_document) {
    std::erase_if(m_nodes, [&] (const auto& id) {
      return !m_document->find(id);
    });
    properties = extract_common_properties(*m_document, m_nodes, m_measurer);
    for(auto& id : m_nodes) {
      if(auto source = get_source(*m_document, id); !source.is_nil()) {
        m_sources.push_back(source);
      }
    }
  } else {
    m_nodes.clear();
  }
  m_current = m_nodes.empty() ? NodeId() : m_nodes.back();
  auto is_same = is_same_structure(properties, m_properties);
  m_properties = std::move(properties);
  if(is_same && !m_is_rebuild_pending && !m_sections.empty()) {
    update_fields();
  } else if(m_is_committing) {
    if(!m_is_rebuild_pending) {
      m_is_rebuild_pending = true;
      QTimer::singleShot(0, this, [this] {
        if(m_is_rebuild_pending) {
          rebuild();
        }
      });
    }
  } else {
    rebuild();
  }
}

void PropertyInspector::rebuild() {
  m_is_rebuild_pending = false;
  ++m_generation;
  if(!m_document) {
    m_properties.clear();
  }
  auto scroll = m_scroll->verticalScrollBar()->value();
  m_fields.clear();
  m_sections.clear();
  auto old = m_scroll->takeWidget();
  if(old) {
    old->deleteLater();
  }
  m_content = new QWidget();
  m_content->setObjectName("inspector_content");
  m_layout = new QVBoxLayout(m_content);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);
  auto add_rule = [&] {
    m_layout->addWidget(make_rule(Qt::Horizontal));
  };
  if(m_workspace == Workspace::PROTOTYPE) {
    auto frame = make_section("Interaction", false);
    auto add = make_icon_button(Glyph::PLUS, "Add Interaction");
    add->setEnabled(false);
    frame.m_header->addWidget(add);
    frame.m_layout->addWidget(make_static_combo("Trigger", "Tap"));
    frame.m_layout->addWidget(make_static_combo("Type", "Transition"));
    frame.m_layout->addWidget(make_static_combo("Destination", ""));
    frame.m_layout->addWidget(make_static_combo("Animation", "None"));
    frame.m_layout->addWidget(make_static_combo("Easing", "Ease In"));
    frame.m_layout->addWidget(make_static_combo("Duration", "0.2 s"));
    m_layout->addWidget(frame.m_widget);
    m_sections.emplace_back("interaction", frame.m_widget);
  } else if(m_workspace == Workspace::SHARE) {
    auto frame = make_section("Link Settings", false);
    frame.m_layout->addWidget(make_static_combo("Link", "Untitled"));
    frame.m_layout->addWidget(
      make_static_combo("View Setting", "Design Review"));
    frame.m_layout->addWidget(
      make_static_combo("Link Access", "Anyone with the link"));
    auto create = new QToolButton();
    create->setObjectName("text_button");
    create->setText("Create Link");
    create->setEnabled(false);
    frame.m_layout->addWidget(create);
    m_layout->addWidget(frame.m_widget);
    m_sections.emplace_back("link_settings", frame.m_widget);
  } else {
    auto is_empty = m_properties.empty();
    auto sections = is_empty ? make_empty_sections() : m_properties;
    auto binding = Binding();
    binding.m_find = [sections = std::make_shared<
        std::vector<PropertyDescriptor>>(sections)] (std::string_view path) {
      return find_property(*sections, path);
    };
    binding.m_editor = [this, is_empty] (
        const PropertyDescriptor& property) -> const PropertyEditor* {
      if(is_empty || !m_editor || property.m_is_read_only) {
        return nullptr;
      }
      return m_property_editors.find(property.m_path);
    };
    auto generation = m_generation;
    binding.m_commit = [this, generation] (
        const std::string& path, const PropertyValue& value) {
      commit(generation, path, value);
    };
    binding.m_finish = [this, generation] (const std::string& path) {
      finish(generation, path);
    };
    binding.m_register = [this] (const std::string& path, QWidget* input,
        bool is_editable, Updater update) {
      m_fields.push_back(std::make_unique<Field>(
        path, input, is_editable, std::move(update)));
    };
    binding.m_actions = m_actions;
    binding.m_is_enabled = !is_empty;
    auto current = static_cast<SectionFrame*>(nullptr);
    auto frames = std::vector<SectionFrame>();
    frames.reserve(sections.size());
    for(auto& section : sections) {
      auto& path = section.m_path;
      if(!continues_group(path) || !current) {
        if(current) {
          add_rule();
        }
        frames.push_back(make_section(
          has_header(path) ? section.m_label : QString(), has_info(path)));
        current = &frames.back();
        current->m_widget->setEnabled(!is_empty);
        m_layout->addWidget(current->m_widget);
      }
      auto& frame = *current;
      if(path == "align") {
        build_align(binding, *frame.m_layout);
      } else if(path == "boolean") {
        build_boolean(binding, *frame.m_layout);
      } else if(path == "component") {
        build_component(binding, frame);
      } else if(path == "transform") {
        build_transform(binding, frame);
      } else if(path == "layout") {
        build_layout(binding, frame);
      } else if(path == "text") {
        build_text(binding, frame);
      } else if(path == "appearance") {
        build_appearance(binding, frame);
      } else if(path == "geometry") {
        build_geometry(binding, frame);
      } else if(path == "fill") {
        build_fill(binding, frame);
      } else if(path == "stroke") {
        build_stroke(binding, frame);
      } else if(path == "grid") {
        build_grid(binding, frame);
      } else if(path == "effects") {
        build_effects(binding, frame);
      } else if(path == "export") {
        build_export(binding, frame);
      } else {
        build_generic(binding, section, *frame.m_layout);
      }
      m_sections.emplace_back(path, frame.m_widget);
    }
    if(current) {
      add_rule();
    }
    for(auto& field : m_fields) {
      if(auto property = find_property(sections, field->m_path)) {
        field->m_update(*property);
      }
    }
    if(is_empty) {
      m_properties.clear();
    }
  }
  m_layout->addStretch(1);
  m_scroll->setWidget(m_content);
  m_scroll->verticalScrollBar()->setValue(scroll);
}

void PropertyInspector::update_fields() {
  for(auto& field : m_fields) {
    auto property = find_property(m_properties, field->m_path);
    if(!property) {
      continue;
    }
    if(m_is_committing || !field->m_input->hasFocus()) {
      field->m_update(*property);
    }
  }
}

void PropertyInspector::commit(
    int generation, const std::string& path, const PropertyValue& value) {
  if(generation != m_generation || !m_editor) {
    return;
  }
  auto editor = m_property_editors.find(path);
  if(!editor) {
    return;
  }
  auto& history = m_editor->get_history();
  if(path != m_edited_path) {
    history.seal();
    m_edited_path = path;
  }
  auto command = editor->m_setter(m_editor->get_document(), m_nodes, value);
  m_is_committing = true;
  if(command) {
    try {
      m_editor->merge(std::move(command));
    } catch(const std::exception&) {}
  }
  if(!m_is_rebuild_pending) {
    update_fields();
  }
  m_is_committing = false;
}

void PropertyInspector::finish(int generation, const std::string& path) {
  if(generation != m_generation || !m_editor || path != m_edited_path) {
    return;
  }
  m_editor->get_history().seal();
  m_edited_path.clear();
}

bool PropertyInspector::is_affected(const ChangeSet& changes) const {
  auto is_related = [&] (const NodeId& id) {
    for(auto& node : m_nodes) {
      if(node == id || is_ancestor(*m_document, node, id)) {
        return true;
      }
    }
    return std::find(m_sources.begin(), m_sources.end(), id) !=
      m_sources.end();
  };
  for(auto& change : changes.m_modified) {
    if(is_related(change.m_id)) {
      return true;
    }
  }
  for(auto& id : changes.m_moved) {
    if(is_related(id)) {
      return true;
    }
  }
  for(auto& id : changes.m_inserted) {
    if(is_related(id)) {
      return true;
    }
  }
  return !changes.m_removed.empty() && std::any_of(m_nodes.begin(),
    m_nodes.end(), [&] (const auto& id) {
      return !m_document->get(id).m_children.empty();
    });
}

void PropertyInspector::on_change(const ChangeSet& changes) {
  if(m_nodes.empty()) {
    return;
  }
  auto is_removed = std::any_of(m_nodes.begin(), m_nodes.end(),
    [&] (const auto& id) {
      return !m_document->find(id);
    });
  if(is_removed || is_affected(changes) || !changes.m_components.empty()) {
    refresh();
  }
}
