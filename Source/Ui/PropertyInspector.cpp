#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>
#include <QAbstractButton>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QConicalGradient>
#include <QFocusEvent>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include "Rellu/Commands/ArtboardCommands.hpp"
#include "Rellu/Commands/AssetCommands.hpp"
#include "Rellu/Commands/Components.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Model/ReferenceResolution.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Ui/PropertyInspector.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"
#include "FieldKeys.hpp"
#include "HyperlinkStatus.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto LARGE_STEP = 10.0;
  constexpr auto SIDE_MARGIN = 16;
  constexpr auto ALIGN_MARGIN = 10;
  constexpr auto ICON_BUTTON_SIZE = 30;
  constexpr auto ICON_SIZE = 20;
  constexpr auto SEGMENT_SIZE = 32;
  constexpr auto STROKE_SEGMENT_WIDTH = 35;
  constexpr auto SWATCH_WIDTH = 30;
  constexpr auto SWATCH_HEIGHT = 18;
  const auto MIXED_SWATCH_BORDER = QColor(0xB3, 0xB3, 0xB3);
  const auto MIXED_SWATCH_RING = QColor(0xF1, 0xED, 0xE9);
  const auto MIXED_SWATCH_FILL = QColor(0xFB, 0xF7, 0xF3);
  const auto MIXED_SWATCH_DASH = QColor(0xC2, 0xC2, 0xC2);
  const auto EM_DASH = QString(QChar(0x2014));
  const auto DEGREE = QString(QChar(0x00B0));
  const auto BLEND_SEPARATORS = QStringList(
    {"Normal", "Color Burn", "Color Dodge", "Hard Light", "Exclusion"});

  QColor to_qcolor(const Color& color) {
    return QColor::fromRgbF(static_cast<float>(color.m_red),
      static_cast<float>(color.m_green), static_cast<float>(color.m_blue),
      static_cast<float>(color.m_alpha));
  }

  void list_swatches(const Resources& resources, const std::string& group,
      std::vector<ColorSwatch>& swatches) {
    for(auto& entry : list_assets(resources, group)) {
      if(!entry.m_group.empty()) {
        list_swatches(resources, entry.m_group, swatches);
        continue;
      }
      for(auto& swatch : resources.m_swatches) {
        if(swatch.m_id == entry.m_asset) {
          swatches.push_back(swatch);
        }
      }
    }
  }

  std::vector<ColorSwatch> get_listed_swatches(const Resources& resources) {
    auto swatches = std::vector<ColorSwatch>();
    list_swatches(resources, COLOR_SECTION, swatches);
    return swatches;
  }

  NodeId get_source(const Document& document, const NodeId& id) {
    if(auto resolution = resolve_reference(document.get_nodes(), id)) {
      return resolution->m_source;
    }
    return NodeId();
  }

  void split_corners(std::vector<PropertyDescriptor>& properties) {
    for(auto& property : properties) {
      if(auto choice = std::get_if<Choice>(&property.m_value);
          choice && property.m_path == "geometry.radii") {
        choice->m_index = 1;
        property.m_is_mixed = false;
      }
      split_corners(property.m_children);
    }
  }

  const auto GRID_SETTING_EDITOR = PropertyEditor();

  bool is_grid_setting(std::string_view path) {
    return path == "grid.visible" || path == "grid.type";
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

  class OverlayScrollArea : public QScrollArea {
    public:
      OverlayScrollArea()
          : m_bar(new QScrollBar(Qt::Vertical, this)) {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_bar->hide();
        auto source = verticalScrollBar();
        QObject::connect(source, &QScrollBar::rangeChanged, m_bar,
          [this] (int minimum, int maximum) {
            m_bar->setRange(minimum, maximum);
            m_bar->setPageStep(viewport()->height());
            m_bar->setVisible(maximum > minimum);
            m_bar->raise();
          });
        QObject::connect(
          source, &QScrollBar::valueChanged, m_bar, &QScrollBar::setValue);
        QObject::connect(
          m_bar, &QScrollBar::valueChanged, source, &QScrollBar::setValue);
      }

    protected:
      void resizeEvent(QResizeEvent* event) override {
        QScrollArea::resizeEvent(event);
        auto extent = m_bar->sizeHint().width();
        m_bar->setGeometry(width() - extent, 0, extent, height());
        m_bar->setPageStep(viewport()->height());
      }

    private:
      QScrollBar* m_bar;
  };

  class Slider : public QSlider {
    public:
      Slider()
        : QSlider(Qt::Horizontal) {}

    protected:
      void wheelEvent(QWheelEvent* event) override {
        event->ignore();
      }
  };

  class ChoiceBox : public QComboBox {
    protected:
      void wheelEvent(QWheelEvent* event) override {
        event->ignore();
      }

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

  void collect_nodes(
      const Document& document, const NodeId& id, std::vector<Node>& nodes) {
    auto node = document.find(id);
    if(!node) {
      return;
    }
    nodes.push_back(*node);
    for(auto& child : node->m_children) {
      collect_nodes(document, child, nodes);
    }
  }

  void restore_nodes(const Document& document, Transaction& transaction,
      const std::vector<Node>& nodes) {
    for(auto& node : nodes) {
      auto current = document.find(node.m_header.m_id);
      if(!current) {
        continue;
      }
      if(current->m_header != node.m_header) {
        transaction.replace_header(node.m_header);
      }
      if(current->m_payload != node.m_payload) {
        transaction.replace_payload(node.m_header.m_id, node.m_payload);
      }
    }
  }

  struct Binding {
    std::function<const PropertyDescriptor* (std::string_view)> m_find;
    std::function<const PropertyEditor* (const PropertyDescriptor&)> m_editor;
    std::function<void (const std::string&, const PropertyValue&)> m_commit;
    std::function<void (const std::string&)> m_finish;
    std::function<void (const std::string&, const PropertyValue&)> m_preview;
    std::function<void (const std::string&)> m_cancel;
    std::function<void ()> m_release;
    std::function<void (const std::string&, QWidget*, bool, Updater)>
      m_register;
    std::function<void (QWidget&, const std::string&)> m_pick;
    ActionRegistry* m_actions;
    bool m_is_enabled;
  };

  QLabel* make_label(const QString& text, const char* name = "field_label") {
    auto label = new QLabel(translate("inspector", text));
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
    button->setToolTip(translate("inspector", tip));
    button->setAccessibleName(translate("inspector", tip));
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

  QToolButton* make_toggle(const Binding& binding, const std::string& path,
      Glyph glyph, const QString& tip) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto button = make_icon_button(glyph, tip);
    button->setCheckable(true);
    button->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(button, &QToolButton::clicked, button,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          commit(path, button->isChecked());
          finish(path);
        });
    }
    binding.m_register(path, button, static_cast<bool>(editor),
      [=] (const PropertyDescriptor& property) {
        auto value = std::get_if<bool>(&property.m_value);
        button->setChecked(value && *value && !property.m_is_mixed);
      });
    return button;
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
    button->setText(translate("inspector", text));
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

  QToolButton* make_command_button(const Binding& binding,
      const QString& text, const std::string& path) {
    auto button = make_text_button(binding, text, nullptr);
    auto probe = PropertyDescriptor();
    probe.m_path = path;
    auto editor = binding.m_editor(probe);
    button->setAccessibleName(translate("inspector", text));
    button->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(button, &QToolButton::clicked, button,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          commit(path, true);
          finish(path);
        });
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
      auto heading = translate("inspector.section", title);
      header->addWidget(make_label(heading.toUpper(), "section_heading"));
      if(has_info) {
        auto info = new QLabel();
        info->setPixmap(get_icon(Glyph::INFO).pixmap(QSize(14, 14)));
        info->setToolTip(heading);
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
    line->setAccessibleName(translate("inspector", property->m_label));
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
    auto to_value = [=] (double number) -> PropertyValue {
      if(is_integer) {
        return static_cast<int>(std::lround(number));
      }
      return number;
    };
    auto commit = [=, commit = binding.m_commit] (double number) {
      commit(path, to_value(number));
    };
    if(editor) {
      auto cancel = [=, cancel = binding.m_cancel] {
        cancel(path);
        line->setText(*shown);
      };
      QObject::connect(line, &QLineEdit::editingFinished, line, [=] {
        if(line->text() == *shown) {
          cancel();
        } else if(auto number = parse(line->text())) {
          commit(*number);
        } else {
          cancel();
        }
      });
      if(editor->m_is_live) {
        QObject::connect(line, &QLineEdit::textEdited, line,
          [=, preview = binding.m_preview] (const QString& text) {
            if(auto number = parse(text)) {
              preview(path, to_value(*number));
            }
          });
      }
      bind_field_keys(*line, cancel, binding.m_release);
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
      const QString& text, bool is_entered = false) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto placeholder = translate("inspector", text);
    auto editor = binding.m_editor(*property);
    auto line = new FieldLine();
    line->setObjectName("text_field");
    line->setAccessibleName(translate("inspector", property->m_label));
    line->setPlaceholderText(placeholder);
    line->setEnabled(binding.m_is_enabled);
    line->setReadOnly(!editor);
    auto shown = std::make_shared<QString>();
    if(editor) {
      bind_field_keys(*line, [=] {
        line->setText(*shown);
      }, binding.m_release);
    }
    if(editor && is_entered) {
      QObject::connect(line, &QLineEdit::editingFinished, line,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          if(line->isModified()) {
            line->setModified(false);
            commit(path, line->text());
          }
          finish(path);
        });
    } else if(editor) {
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
        *shown = text;
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
    auto box = new QCheckBox(translate("inspector", text));
    box->setFocusPolicy(Qt::NoFocus);
    box->setAccessibleName(translate("inspector", property->m_label));
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
    toggle->setAccessibleName(translate("inspector", property->m_label));
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
    box->setAccessibleName(translate("inspector", property->m_label));
    box->setPlaceholderText(EM_DASH);
    auto options = QStringList();
    if(auto choice = std::get_if<Choice>(&property->m_value)) {
      options = choice->m_options;
    } else {
      options.push_back(to_text(property->m_value));
    }
    auto is_translated = std::holds_alternative<Choice>(property->m_value) &&
      path != "text.family" && path != "text.style" &&
      path != "component.state";
    for(auto i = 0; i != options.size(); ++i) {
      if(is_translated) {
        box->addItem(translate("inspector." + path, options[i]), i);
      } else {
        box->addItem(options[i], i);
      }
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
      std::vector<Segment> segments, bool is_toggle = false,
      int width = SEGMENT_SIZE + 6) {
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
        button->setFixedSize(width, SEGMENT_SIZE);
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

  void paint_swatch(QToolButton& button, const PropertyDescriptor* property) {
    auto ratio = button.devicePixelRatioF();
    auto pixmap = QPixmap(static_cast<int>(SWATCH_WIDTH * ratio),
      static_cast<int>(SWATCH_HEIGHT * ratio));
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    auto painter = QPainter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    auto bounds = QRectF(0.5, 0.5, SWATCH_WIDTH - 1, SWATCH_HEIGHT - 1);
    if(property && property->m_is_mixed) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(MIXED_SWATCH_RING);
      painter.drawRoundedRect(bounds, 2, 2);
      painter.setBrush(MIXED_SWATCH_FILL);
      painter.drawRect(bounds.adjusted(1.5, 1.5, -1.5, -1.5));
      painter.setBrush(MIXED_SWATCH_DASH);
      painter.drawRect(QRectF(SWATCH_WIDTH / 2.0 - 4, SWATCH_HEIGHT / 2.0 - 1,
        8, 2));
      painter.setPen(QPen(MIXED_SWATCH_BORDER, 1.25));
      painter.setBrush(Qt::NoBrush);
      painter.drawRoundedRect(bounds, 2, 2);
      painter.end();
      button.setIcon(QIcon(pixmap));
      return;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::white);
    painter.drawRoundedRect(bounds, 2, 2);
    painter.setBrush(QColor(0xD0, 0xD0, 0xD0));
    for(auto y = 0; y < SWATCH_HEIGHT; y += 6) {
      for(auto x = (y / 6) % 2 * 6; x < SWATCH_WIDTH; x += 12) {
        painter.drawRect(QRectF(x, y, 6, 6).intersected(bounds));
      }
    }
    auto fill = Fill();
    if(property && !property->m_is_mixed) {
      if(auto color = std::get_if<Color>(&property->m_value)) {
        fill = SolidFill(*color);
      } else if(auto value = std::get_if<Fill>(&property->m_value)) {
        fill = *value;
      }
    }
    if(auto solid = std::get_if<SolidFill>(&fill)) {
      painter.setBrush(to_qcolor(solid->m_color));
      painter.drawRoundedRect(bounds, 2, 2);
    } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
      auto set_stops = [&] (QGradient& paint) {
        for(auto& stop : gradient->m_stops) {
          paint.setColorAt(
            std::clamp(stop.m_offset, 0.0, 1.0), to_qcolor(stop.m_color));
        }
      };
      if(std::holds_alternative<RadialGradient>(gradient->m_geometry)) {
        auto paint = QRadialGradient(bounds.center(), bounds.width() / 2);
        set_stops(paint);
        painter.setBrush(paint);
      } else if(auto angular =
          std::get_if<AngularGradient>(&gradient->m_geometry)) {
        auto paint = QConicalGradient(bounds.center(), -angular->m_rotation);
        for(auto& stop : gradient->m_stops) {
          paint.setColorAt(1 - std::clamp(stop.m_offset, 0.0, 1.0),
            to_qcolor(stop.m_color));
        }
        painter.setBrush(paint);
      } else {
        auto paint = QLinearGradient(bounds.topLeft(), bounds.topRight());
        set_stops(paint);
        painter.setBrush(paint);
      }
      painter.drawRoundedRect(bounds, 2, 2);
    } else if(std::holds_alternative<ImageFill>(fill)) {
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

  QToolButton* make_swatch(const Binding& binding, const std::string& path) {
    auto button = new QToolButton();
    button->setObjectName("swatch");
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setIconSize(QSize(SWATCH_WIDTH, SWATCH_HEIGHT));
    button->setFixedSize(SWATCH_WIDTH + 4, SWATCH_HEIGHT + 4);
    auto property = binding.m_find(path);
    auto editor = property ? binding.m_editor(*property) : nullptr;
    button->setEnabled(binding.m_is_enabled);
    if(property) {
      button->setAccessibleName(translate("inspector", property->m_label));
    }
    if(editor) {
      QObject::connect(button, &QToolButton::clicked, button,
        [=, pick = binding.m_pick] {
          if(pick) {
            pick(*button, path);
          }
        });
    }
    if(property) {
      binding.m_register(path, button, static_cast<bool>(editor),
        [=] (const PropertyDescriptor& property) {
          paint_swatch(*button, &property);
        });
    } else {
      paint_swatch(*button, nullptr);
    }
    return button;
  }

  QToolButton* make_eyedropper(
      const Binding& binding, const std::string& color_path) {
    auto dropper =
      make_icon_button(Glyph::EYEDROPPER, "Pick color from screen");
    dropper->setObjectName("icon_toggle");
    auto probe = PropertyDescriptor();
    probe.m_path = color_path;
    auto property = binding.m_find(color_path);
    auto editor = binding.m_editor(property ? *property : probe);
    dropper->setEnabled(binding.m_is_enabled && editor);
    if(editor) {
      QObject::connect(dropper, &QToolButton::clicked, dropper,
        [=, commit = binding.m_commit, finish = binding.m_finish] {
          pick_screen_color([=] (const Color& color) {
            commit(color_path, color);
            finish(color_path);
          });
        });
    }
    return dropper;
  }

  QWidget* make_style_row(const Binding& binding, const std::string& path,
      const QString& label, const std::string& swatch_path,
      const std::string& dropper_path) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 14);
    if(auto box = make_check(binding, path)) {
      layout->addWidget(box);
    }
    layout->addWidget(make_swatch(binding, swatch_path));
    layout->addWidget(make_label(label, "row_label"), 1);
    if(!dropper_path.empty()) {
      layout->addWidget(make_eyedropper(binding, dropper_path));
    }
    return row;
  }

  QWidget* make_slider(const Binding& binding, const std::string& path,
      const QString& label, double minimum, double maximum) {
    auto property = binding.m_find(path);
    if(!property) {
      return nullptr;
    }
    auto editor = binding.m_editor(*property);
    auto display = get_display(path, editor);
    auto container = new QWidget();
    auto column = new QVBoxLayout(container);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(4);
    column->addWidget(make_label(label));
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 16);
    auto slider = new Slider();
    slider->setObjectName("opacity_slider");
    slider->setAccessibleName(translate("inspector", property->m_label));
    slider->setFocusPolicy(Qt::NoFocus);
    auto scale = display.m_scale;
    slider->setRange(static_cast<int>(std::lround(minimum * scale)),
      static_cast<int>(std::lround(maximum * scale)));
    slider->setEnabled(binding.m_is_enabled && editor);
    layout->addWidget(slider, 1);
    layout->addWidget(
      make_number(binding, path, QString(), std::nullopt, 44));
    column->addWidget(row);
    if(editor) {
      QObject::connect(slider, &QSlider::sliderMoved, slider,
        [=, commit = binding.m_commit] (int value) {
          commit(path, value / scale);
        });
      QObject::connect(slider, &QSlider::sliderReleased, slider,
        [=, finish = binding.m_finish] {
          finish(path);
        });
    }
    binding.m_register(path, slider, false,
      [=] (const PropertyDescriptor& property) {
        if(auto value = std::get_if<double>(&property.m_value)) {
          if(!slider->isSliderDown()) {
            slider->setValue(static_cast<int>(std::lround(*value * scale)));
          }
        }
      });
    return container;
  }

  void build_align(const Binding& binding, QVBoxLayout& layout) {
    auto margins = layout.contentsMargins();
    layout.setContentsMargins(
      ALIGN_MARGIN, margins.top(), ALIGN_MARGIN, margins.bottom());
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
    if(auto lock = make_toggle(binding, "transform.lock_aspect",
        Glyph::LOCK_ASPECT, "Lock Aspect")) {
      lock->setFixedSize(16, 36);
      lock->setIconSize(QSize(12, 12));
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

  class PinDiagram : public QWidget {
    public:
      std::array<bool, 4> m_pins = std::array<bool, 4>();
      std::function<void (int side)> m_toggle;

      PinDiagram() {
        setObjectName("pin_diagram");
        setFixedSize(64, 64);
        setCursor(Qt::PointingHandCursor);
      }

    protected:
      void paintEvent(QPaintEvent*) override {
        auto painter = QPainter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto outer = QRectF(1.5, 1.5, width() - 3, height() - 3);
        auto inner = QRectF(22.5, 22.5, width() - 45, height() - 45);
        auto ink = isEnabled() ? Colors::ICON : Colors::DISABLED_TEXT;
        painter.setPen(QPen(Colors::BORDER, 1));
        painter.setBrush(Colors::BUTTON);
        painter.drawRect(outer);
        painter.setPen(QPen(ink, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(inner);
        auto center = outer.center();
        auto lines = std::array<QLineF, 4>({
          QLineF(center.x(), outer.top() + 4, center.x(), inner.top() - 3),
          QLineF(inner.right() + 3, center.y(), outer.right() - 4, center.y()),
          QLineF(center.x(), inner.bottom() + 3, center.x(),
            outer.bottom() - 4),
          QLineF(outer.left() + 4, center.y(), inner.left() - 3, center.y())});
        for(auto i = 0; i != 4; ++i) {
          auto color = m_pins[i] && isEnabled() ? Colors::ACTIVE :
            m_pins[i] ? Colors::SECONDARY_TEXT : Colors::BORDER;
          auto pen = QPen(color, m_pins[i] ? 2.0 : 1.0);
          if(!m_pins[i]) {
            pen.setDashPattern({2, 2});
          }
          painter.setPen(pen);
          painter.drawLine(lines[i]);
        }
      }

      void mousePressEvent(QMouseEvent* event) override {
        auto position = event->position();
        auto center = QPointF(width() / 2.0, height() / 2.0);
        auto dx = position.x() - center.x();
        auto dy = position.y() - center.y();
        if(std::abs(dx) < 10 && std::abs(dy) < 10) {
          return;
        }
        auto side = 0;
        if(std::abs(dx) > std::abs(dy)) {
          side = dx > 0 ? 1 : 3;
        } else {
          side = dy > 0 ? 2 : 0;
        }
        if(m_toggle) {
          m_toggle(side);
        }
      }
  };

  void build_constraints(const Binding& binding, SectionFrame& frame) {
    auto mode = binding.m_find("layout.responsive.mode");
    if(!mode) {
      return;
    }
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto modes = make_row(layout, 0);
    auto editor = binding.m_editor(*mode);
    auto buttons = std::make_shared<std::vector<QToolButton*>>();
    for(auto [index, text] : {std::pair(0, "Auto"), std::pair(1, "Manual")}) {
      auto button = new QToolButton();
      button->setObjectName("text_button");
      button->setFocusPolicy(Qt::NoFocus);
      button->setCheckable(true);
      button->setText(translate("inspector.layout.resize", text));
      button->setAccessibleName(button->text());
      button->setEnabled(binding.m_is_enabled && editor);
      layout->addWidget(button, 1);
      buttons->push_back(button);
      if(editor) {
        QObject::connect(button, &QToolButton::clicked, button,
          [=, commit = binding.m_commit, finish = binding.m_finish] {
            commit("layout.responsive.mode",
              Choice(index, QStringList({"Auto", "Manual"})));
            finish("layout.responsive.mode");
          });
      }
    }
    binding.m_register("layout.responsive.mode", modes, false,
      [=] (const PropertyDescriptor& property) {
        auto choice = std::get_if<Choice>(&property.m_value);
        for(auto i = std::size_t(0); i != buttons->size(); ++i) {
          (*buttons)[i]->setChecked(choice && !property.m_is_mixed &&
            choice->m_index == static_cast<int>(i));
        }
      });
    frame.m_layout->addWidget(modes);
    auto pins = make_row(layout, 16);
    auto diagram = new PinDiagram();
    auto paths = std::array<std::string, 4>({"layout.responsive.top",
      "layout.responsive.right", "layout.responsive.bottom",
      "layout.responsive.left"});
    auto is_editable = false;
    for(auto i = 0; i != 4; ++i) {
      auto property = binding.m_find(paths[i]);
      if(!property) {
        continue;
      }
      auto pin_editor = binding.m_editor(*property);
      is_editable |= static_cast<bool>(pin_editor);
      binding.m_register(paths[i], diagram, static_cast<bool>(pin_editor),
        [=] (const PropertyDescriptor& property) {
          auto value = std::get_if<bool>(&property.m_value);
          diagram->m_pins[i] = value && *value && !property.m_is_mixed;
          diagram->update();
        });
    }
    diagram->setEnabled(binding.m_is_enabled && is_editable);
    diagram->m_toggle = [=, commit = binding.m_commit,
        finish = binding.m_finish] (int side) {
      commit(paths[side], !diagram->m_pins[side]);
      finish(paths[side]);
    };
    layout->addWidget(diagram);
    auto checks = new QWidget();
    auto checks_layout = new QVBoxLayout(checks);
    checks_layout->setContentsMargins(0, 0, 0, 0);
    checks_layout->setSpacing(10);
    if(auto box =
        make_check(binding, "layout.responsive.width", "Fix Width")) {
      checks_layout->addWidget(box);
    }
    if(auto box =
        make_check(binding, "layout.responsive.height", "Fix Height")) {
      checks_layout->addWidget(box);
    }
    layout->addWidget(checks, 1);
    frame.m_layout->addWidget(pins);
  }

  void build_layout(const Binding& binding, SectionFrame& frame) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout);
    layout->addWidget(make_label("Responsive Resize", "row_label"), 1);
    if(auto toggle = make_switch(binding, "layout.responsive")) {
      layout->addWidget(toggle);
    }
    frame.m_layout->addWidget(row);
    build_constraints(binding, frame);
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
    if(binding.m_find("layout.spacing_before") ||
        binding.m_find("layout.spacing_after")) {
      auto spacings = make_row(layout, 10);
      layout->addWidget(make_label("Adjacent Spacings", "row_label"), 1);
      for(auto path : {"layout.spacing_before", "layout.spacing_after"}) {
        if(auto field = make_number(binding, path)) {
          layout->addWidget(field, 1);
        }
      }
      frame.m_layout->addWidget(spacings);
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
        make_text_field(binding, "text.hyperlink", "Insert URL here", true)) {
      layout->addWidget(field, 1);
      link = add_hyperlink_status(link, *static_cast<QLineEdit*>(field));
    }
    frame.m_layout->addWidget(link);
  }

  void build_appearance(const Binding& binding, SectionFrame& frame) {
    auto opacity = binding.m_find("appearance.opacity");
    if(opacity) {
      frame.m_layout->addWidget(make_label("Opacity"));
      auto layout = static_cast<QHBoxLayout*>(nullptr);
      auto row = make_row(layout, 16);
      auto slider = new Slider();
      slider->setObjectName("opacity_slider");
      slider->setAccessibleName(translate("inspector", "Opacity"));
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
    frame.m_layout->addWidget(make_style_row(
      binding, "fill.enabled", "Fill", "fill.paint", "fill.color"));
  }

  void build_stroke(const Binding& binding, SectionFrame& frame) {
    frame.m_layout->addWidget(make_style_row(binding, "stroke.enabled",
      "Border", "stroke.color", "stroke.color"));
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
        Segment(0, Glyph::STROKE_CENTER, "Center Stroke")}, false,
        STROKE_SEGMENT_WIDTH)) {
      layout->addWidget(segments);
    }
    if(auto segments = make_segments(binding, "stroke.cap", {
        Segment(0, Glyph::CAP_BUTT, "Butt Cap"),
        Segment(1, Glyph::CAP_ROUND, "Round Cap"),
        Segment(2, Glyph::CAP_PROJECTING, "Projecting Cap")}, false,
        STROKE_SEGMENT_WIDTH)) {
      layout->addWidget(segments);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(ends);
    auto joins = make_row(layout, 18);
    layout->addSpacing(28);
    if(auto segments = make_segments(binding, "stroke.join", {
        Segment(0, Glyph::JOIN_MITER, "Miter Join"),
        Segment(1, Glyph::JOIN_ROUND, "Round Join"),
        Segment(2, Glyph::JOIN_BEVEL, "Bevel Join")}, false,
        STROKE_SEGMENT_WIDTH)) {
      layout->addWidget(segments);
    }
    layout->addStretch(1);
    frame.m_layout->addWidget(joins);
  }

  void build_grid(const Binding& binding, SectionFrame& frame) {
    auto layout = static_cast<QHBoxLayout*>(nullptr);
    auto row = make_row(layout, 14);
    if(auto box = make_check(binding, "grid.visible")) {
      box->setToolTip(translate("inspector", "Show/Hide Grid"));
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
      layout->addWidget(make_swatch(binding, "grid.column_color"));
      layout->addWidget(make_label("Columns", "row_label"), 1);
      layout->addWidget(make_number(
        binding, "grid.columns", QString(), std::nullopt, 40));
      frame.m_layout->addWidget(columns);
      for(auto [path, label] : {std::pair("grid.gutter", "Gutter Width"),
          std::pair("grid.column_width", "Column Width")}) {
        auto field_row = make_row(layout, 12);
        layout->addWidget(make_label(label), 1);
        auto field = make_number(binding, path, QString(), std::nullopt, 40);
        if(field && std::string_view(path) == "grid.column_width") {
          for(auto line : field->findChildren<QLineEdit*>()) {
            auto font = line->font();
            font.setItalic(true);
            line->setFont(font);
          }
        }
        layout->addWidget(field);
        frame.m_layout->addWidget(field_row);
      }
      auto margins = make_row(layout, 12);
      layout->addWidget(make_segments(binding, "grid.linked_margins", {
        Segment(1, Glyph::MARGINS_LINKED, "Linked Margins"),
        Segment(0, Glyph::MARGINS_SEPARATE, "Different Margins")}));
      if(binding.m_find("grid.margin_linked")) {
        layout->addStretch(1);
        layout->addWidget(make_number(
          binding, "grid.margin_linked", QString(), std::nullopt, 40));
      } else {
        for(auto path : {"grid.margin_top", "grid.margin_right",
            "grid.margin_bottom", "grid.margin_left"}) {
          layout->addWidget(make_number(binding, path), 1);
        }
      }
      frame.m_layout->addWidget(margins);
      auto defaults = make_row(layout, 0);
      layout->addWidget(make_command_button(
        binding, "Use Default", "grid.use_default"), 1);
      layout->addWidget(make_command_button(
        binding, "Make Default", "grid.make_default"), 1);
      frame.m_layout->addWidget(defaults);
    } else {
      auto square = make_row(layout, 12);
      layout->addWidget(make_swatch(binding, "grid.color"));
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
        make_style_row(binding, path, label, path + ".color", ""));
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
        for(auto [path, label, minimum, maximum] : {
            std::tuple("effects.blur.amount", "Blur Amount", 0.0, 50.0),
            std::tuple("effects.blur.brightness", "Brightness", -50.0, 50.0),
            std::tuple("effects.blur.opacity", "Effect Opacity", 0.0, 1.0)}) {
          if(auto slider = make_slider(
              binding, path, label, minimum, maximum)) {
            frame.m_layout->addWidget(slider);
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
        row_layout->addWidget(make_swatch(binding, child.m_path));
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

  bool is_text_appearance(const std::string& path) {
    return path == "fill" || path == "fill.enabled" || path == "fill.color" ||
      path == "stroke.enabled" || path == "stroke.color" ||
      path == "stroke.width";
  }

  void remember_text(Editor& editor, const std::vector<NodeId>& nodes) {
    for(auto& id : nodes) {
      if(auto node = editor.get_document().find(id)) {
        if(auto text = node->get_text()) {
          editor.get_text_selection().remember(*text);
          return;
        }
      }
    }
  }

  QWidget* make_static_combo(const QString& label, const QString& value) {
    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(make_label(label));
    auto box = new ChoiceBox();
    box->setObjectName("choice_field");
    box->addItem(translate("inspector", value));
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
      m_grid_actions(nullptr),
      m_workspace(Workspace::DESIGN),
      m_measurer(estimate_text_bounds),
      m_generation(0),
      m_is_committing(false),
      m_is_rebuild_pending(false),
      m_is_locked(false),
      m_is_corner_split(false),
      m_scroll(new OverlayScrollArea()),
      m_content(nullptr),
      m_layout(nullptr),
      m_picker(new ColorPicker(this)),
      m_footer(nullptr),
      m_is_footer_exclusive(false) {
  setObjectName("inspector");
  m_picker->set_close_handler([this] {
    auto path = std::exchange(m_picked_path, std::string());
    if(!path.empty()) {
      seal(path);
    }
  });
  m_picker->set_change_handler([this] (const Fill& fill) {
    if(m_picked_path.empty()) {
      return;
    } else if(m_picked_path.ends_with(".paint")) {
      apply(m_picked_path, fill);
    } else if(auto solid = std::get_if<SolidFill>(&fill)) {
      apply(m_picked_path, solid->m_color);
    }
  });
  m_picker->set_add_handler([this] (const Fill& fill) {
    add_swatch(fill);
  });
  m_scroll->setObjectName("inspector_scroll");
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_scroll);
  rebuild();
}

PropertyInspector::~PropertyInspector() {
  discard_preview();
}

void PropertyInspector::set_document(Document* document) {
  discard_preview();
  m_selection_subscription.release();
  m_text_selection_subscription.release();
  m_history_subscription.release();
  m_editor = nullptr;
  attach(document);
  rebuild();
}

Editor* PropertyInspector::get_editor() const {
  return m_editor;
}

void PropertyInspector::set_editor(Editor* editor) {
  discard_preview();
  m_selection_subscription.release();
  m_text_selection_subscription.release();
  m_history_subscription.release();
  m_editor = editor;
  if(!m_editor) {
    attach(nullptr);
    rebuild();
    return;
  }
  attach(&m_editor->get_document());
  auto follow = [this] {
    give_initial_grids();
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
  m_text_selection_subscription =
    m_editor->get_text_selection().subscribe([this] {
      refresh();
    });
  m_history_subscription = m_editor->get_history().subscribe([this] {
    give_initial_grids();
  });
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

void PropertyInspector::set_grid_actions(GridActions* actions) {
  m_grid_subscription.release();
  m_grid_actions = actions;
  if(m_grid_actions) {
    m_grid_subscription = m_grid_actions->subscribe([this] {
      refresh();
    });
  }
  rebuild();
}

void PropertyInspector::set_workspace(Workspace workspace) {
  m_workspace = workspace;
  give_initial_grids();
  rebuild();
}

void PropertyInspector::set_footer(QWidget* footer, bool is_exclusive) {
  if(footer == m_footer && is_exclusive == m_is_footer_exclusive) {
    return;
  }
  if(m_footer) {
    m_footer->hide();
    m_footer->setParent(this);
  }
  m_footer = footer;
  m_is_footer_exclusive = is_exclusive;
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
  if(nodes != m_nodes) {
    m_is_corner_split = false;
    if(m_picker->isVisible()) {
      m_picker->hide();
    }
  }
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

void PropertyInspector::give_initial_grids() {
  if(!m_editor || m_workspace != Workspace::DESIGN ||
      m_editor->get_document().is_publishing()) {
    return;
  }
  initialize_grids(
    m_editor->get_document(), m_editor->get_selection().get_nodes());
}

void PropertyInspector::show_grid_settings(
    std::vector<PropertyDescriptor>& properties) const {
  auto section = std::find_if(properties.begin(), properties.end(),
    [] (const auto& property) { return property.m_path == "grid"; });
  if(section == properties.end()) {
    return;
  }
  auto settings = m_grid_actions ? m_grid_actions->get_settings() :
    GridSettings();
  auto is_square = get_offered_grid(settings) == GridType::SQUARE;
  std::erase_if(section->m_children, [&] (const auto& property) {
    auto is_square_property =
      property.m_path == "grid.size" || property.m_path == "grid.color";
    auto is_shared =
      property.m_path == "grid.visible" || property.m_path == "grid.type";
    return !is_shared && is_square_property != is_square;
  });
  for(auto& property : section->m_children) {
    if(property.m_path == "grid.visible") {
      property.m_value = is_grid_visible(settings);
      property.m_is_mixed = false;
    } else if(property.m_path == "grid.type") {
      auto choice = std::get<Choice>(property.m_value);
      choice.m_index = is_square ? 1 : 0;
      property.m_value = std::move(choice);
      property.m_is_mixed = false;
    }
  }
}

void PropertyInspector::refresh() {
  auto properties = std::vector<PropertyDescriptor>();
  m_sources.clear();
  if(m_document) {
    std::erase_if(m_nodes, [&] (const auto& id) {
      return !m_document->find(id);
    });
    auto selection = m_editor ? &m_editor->get_text_selection() : nullptr;
    properties = extract_common_properties(
      *m_document, m_nodes, m_measurer, selection);
    if(m_is_corner_split) {
      split_corners(properties);
    }
    show_grid_settings(properties);
    for(auto& id : m_nodes) {
      if(auto source = get_source(*m_document, id); !source.is_nil()) {
        m_sources.push_back(source);
      }
    }
  } else {
    m_nodes.clear();
  }
  m_current = m_nodes.empty() ? NodeId() : m_nodes.back();
  auto is_locked = find_lock();
  auto is_same = is_same_structure(properties, m_properties) &&
    is_locked == m_is_locked;
  m_is_locked = is_locked;
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
  discard_preview();
  if(auto focus = QApplication::focusWidget();
      focus && m_content && m_content->isAncestorOf(focus)) {
    focus->clearFocus();
    release();
  }
  if(!m_document) {
    m_properties.clear();
  }
  auto scroll = m_scroll->verticalScrollBar()->value();
  m_fields.clear();
  m_sections.clear();
  if(m_footer) {
    m_footer->hide();
    m_footer->setParent(this);
  }
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
    create->setText(translate("inspector", "Create Link"));
    create->setEnabled(false);
    frame.m_layout->addWidget(create);
    m_layout->addWidget(frame.m_widget);
    m_sections.emplace_back("link_settings", frame.m_widget);
  } else {
    auto is_empty = m_properties.empty();
    auto sections = is_empty ? make_empty_sections() : m_properties;
    if(m_footer && m_is_footer_exclusive) {
      sections.clear();
    }
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
      if(is_grid_setting(property.m_path)) {
        return m_grid_actions ? &GRID_SETTING_EDITOR : nullptr;
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
    binding.m_preview = [this, generation] (
        const std::string& path, const PropertyValue& value) {
      preview(generation, path, value);
    };
    binding.m_cancel = [this, generation] (const std::string& path) {
      cancel(generation, path);
    };
    binding.m_release = [this] {
      release();
    };
    binding.m_register = [this] (const std::string& path, QWidget* input,
        bool is_editable, Updater update) {
      m_fields.push_back(std::make_unique<Field>(
        path, input, is_editable, std::move(update)));
    };
    binding.m_pick = [this] (QWidget& anchor, const std::string& path) {
      open_picker(QRect(anchor.mapToGlobal(QPoint(0, 0)), anchor.size()),
        path);
    };
    binding.m_actions = m_actions;
    binding.m_is_enabled = !is_empty && !m_is_locked;
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
        current->m_widget->setEnabled(!is_empty && !m_is_locked);
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
    if(m_footer) {
      m_layout->addWidget(m_footer);
      m_footer->show();
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
    if(field->m_path == m_preview_path && field->m_input->hasFocus()) {
      continue;
    }
    if(m_is_committing || !field->m_input->hasFocus()) {
      field->m_update(*property);
    }
  }
}

ColorPicker& PropertyInspector::get_color_picker() {
  return *m_picker;
}

const std::string& PropertyInspector::get_picked_path() const {
  return m_picked_path;
}

bool PropertyInspector::open_color_picker(std::string_view path) {
  auto field = find_field(path);
  if(!field || !field->isEnabled()) {
    return false;
  }
  open_picker(QRect(field->mapToGlobal(QPoint(0, 0)), field->size()),
    std::string(path));
  return true;
}

bool PropertyInspector::is_locked() const {
  return m_is_locked;
}

void PropertyInspector::set_release_handler(ReleaseHandler handler) {
  m_release_handler = std::move(handler);
}

void PropertyInspector::commit(
    int generation, const std::string& path, const PropertyValue& value) {
  if(generation != m_generation) {
    return;
  }
  if(path == m_preview_path) {
    discard_preview();
  }
  apply(path, value);
}

void PropertyInspector::apply(
    const std::string& path, const PropertyValue& value) {
  if(!m_editor) {
    return;
  }
  if(is_grid_setting(path)) {
    if(m_grid_actions) {
      m_is_committing = true;
      if(path == "grid.visible") {
        m_grid_actions->toggle(
          get_offered_grid(m_grid_actions->get_settings()));
      } else if(auto choice = std::get_if<Choice>(&value)) {
        m_grid_actions->choose(
          choice->m_index == 1 ? GridType::SQUARE : GridType::LAYOUT);
      }
      m_is_committing = false;
    }
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
  auto is_corner_mode = path == "geometry.radii";
  if(auto choice = std::get_if<Choice>(&value); choice && is_corner_mode) {
    m_is_corner_split = choice->m_index == 1;
  }
  auto command = editor->m_setter(m_editor->get_document(), m_nodes, value);
  if(is_corner_mode && !command) {
    refresh();
    return;
  }
  m_is_committing = true;
  if(command) {
    try {
      m_editor->merge(std::move(command));
    } catch(const std::exception&) {}
    if(is_text_appearance(path)) {
      remember_text(*m_editor, m_nodes);
    }
  }
  if(!m_is_rebuild_pending) {
    update_fields();
  }
  m_is_committing = false;
}

void PropertyInspector::finish(int generation, const std::string& path) {
  if(generation != m_generation) {
    return;
  }
  if(path == m_preview_path) {
    discard_preview();
  }
  seal(path);
}

void PropertyInspector::preview(
    int generation, const std::string& path, const PropertyValue& value) {
  if(generation != m_generation || !m_editor) {
    return;
  }
  auto editor = m_property_editors.find(path);
  if(!editor) {
    return;
  }
  auto& document = m_editor->get_document();
  if(path != m_preview_path) {
    discard_preview();
    m_editor->get_history().seal();
    m_edited_path.clear();
    m_preview_path = path;
    for(auto& id : m_nodes) {
      collect_nodes(document, id, m_preview_nodes);
    }
  }
  auto is_committing = std::exchange(m_is_committing, true);
  try {
    auto transaction = Transaction(document);
    restore_nodes(document, transaction, m_preview_nodes);
    if(auto command = editor->m_setter(document, m_nodes, value)) {
      command->apply(document, transaction);
    }
    transaction.commit_unrecorded();
  } catch(const std::exception&) {}
  m_is_committing = is_committing;
}

void PropertyInspector::cancel(int generation, const std::string& path) {
  if(generation == m_generation && path == m_preview_path) {
    discard_preview();
  }
}

void PropertyInspector::discard_preview() {
  if(m_preview_path.empty()) {
    return;
  }
  m_preview_path.clear();
  auto nodes = std::exchange(m_preview_nodes, std::vector<Node>());
  if(!m_editor) {
    return;
  }
  auto& document = m_editor->get_document();
  auto is_committing = std::exchange(m_is_committing, true);
  try {
    auto transaction = Transaction(document);
    restore_nodes(document, transaction, nodes);
    transaction.commit_unrecorded();
  } catch(const std::exception&) {}
  m_is_committing = is_committing;
}

void PropertyInspector::release() {
  if(m_release_handler) {
    m_release_handler();
  }
}

void PropertyInspector::seal(const std::string& path) {
  if(!m_editor || path != m_edited_path) {
    return;
  }
  m_editor->get_history().seal();
  m_edited_path.clear();
}

void PropertyInspector::open_picker(
    const QRect& anchor, const std::string& path) {
  auto property = find_property(m_properties, path);
  if(!property || !m_editor) {
    return;
  }
  if(m_picker->isVisible()) {
    m_picker->hide();
  }
  auto is_fill = path.ends_with(".paint");
  auto fill = Fill(SolidFill());
  if(auto value = std::get_if<Fill>(&property->m_value)) {
    fill = *value;
    if(std::holds_alternative<NoFill>(fill)) {
      fill = SolidFill(Color(1, 1, 1, 1));
    }
  } else if(auto color = std::get_if<Color>(&property->m_value)) {
    fill = SolidFill(*color);
  }
  m_picker->set_gradient_enabled(is_fill);
  m_picker->set_fill(fill);
  m_picker->set_swatches(
    get_listed_swatches(m_editor->get_document().get_resources()));
  m_picked_path = path;
  auto window = this->window();
  auto left = mapToGlobal(QPoint(0, 0)).x();
  if(window) {
    left = std::min(left, window->mapToGlobal(QPoint(0, 0)).x() +
      window->width());
  }
  m_picker->popup(QRect(left, anchor.top(), 1, anchor.height()));
}

void PropertyInspector::add_swatch(const Fill& fill) {
  if(!m_editor) {
    return;
  }
  auto swatch = ColorSwatch();
  if(auto solid = std::get_if<SolidFill>(&fill)) {
    swatch.m_color = solid->m_color;
  } else if(auto gradient = std::get_if<GradientFill>(&fill)) {
    swatch.m_gradient = gradient->m_stops;
    if(!gradient->m_stops.empty()) {
      swatch.m_color = gradient->m_stops.front().m_color;
    }
  } else {
    return;
  }
  auto& swatches = m_editor->get_document().get_resources().m_swatches;
  if(std::any_of(swatches.begin(), swatches.end(), [&] (const auto& entry) {
      return entry.m_color == swatch.m_color &&
        entry.m_gradient == swatch.m_gradient;
    })) {
    return;
  }
  seal(m_edited_path);
  try {
    m_editor->execute(std::make_unique<AddSwatchCommand>(std::move(swatch)));
  } catch(const std::exception&) {}
  m_picker->set_swatches(
    get_listed_swatches(m_editor->get_document().get_resources()));
}

bool PropertyInspector::find_lock() const {
  if(!m_document) {
    return false;
  }
  return std::any_of(m_nodes.begin(), m_nodes.end(), [&] (const auto& id) {
    auto header = find_rendered_header(*m_document, id);
    return header && header->m_is_locked;
  });
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
  if(is_removed || is_affected(changes) || !changes.m_components.empty() ||
      changes.m_is_asset_group_changed) {
    refresh();
  }
}
