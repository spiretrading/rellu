#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <stdexcept>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Prototype/KeyNames.hpp"
#include "Rellu/Prototype/StageDocument.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto SIDE_MARGIN = 16;

  struct Option {
    QString m_label;
    bool m_is_enabled;
  };

  struct Section {
    QWidget* m_widget;
    QVBoxLayout* m_layout;
    QHBoxLayout* m_header;
  };

  const auto TRANSITIONS = std::to_array({Transition::NONE,
    Transition::DISSOLVE, Transition::SLIDE_LEFT, Transition::SLIDE_RIGHT,
    Transition::SLIDE_UP, Transition::SLIDE_DOWN, Transition::PUSH_LEFT,
    Transition::PUSH_RIGHT, Transition::PUSH_UP, Transition::PUSH_DOWN});

  const auto OVERLAY_TRANSITIONS = std::to_array({Transition::NONE,
    Transition::DISSOLVE, Transition::SLIDE_LEFT, Transition::SLIDE_RIGHT,
    Transition::SLIDE_UP, Transition::SLIDE_DOWN});

  const auto STATE_TRANSITIONS = std::to_array(
    {Transition::NONE, Transition::DISSOLVE, Transition::AUTO_ANIMATE});

  const auto EASINGS = std::to_array({Easing::EASE_OUT, Easing::EASE_IN,
    Easing::EASE_IN_OUT, Easing::LINEAR, Easing::SNAP, Easing::WIND_UP,
    Easing::BOUNCE});

  QString to_label(Transition transition) {
    switch(transition) {
      case Transition::DISSOLVE:
        return "Dissolve";
      case Transition::SLIDE_LEFT:
        return "Slide Left";
      case Transition::SLIDE_RIGHT:
        return "Slide Right";
      case Transition::SLIDE_UP:
        return "Slide Up";
      case Transition::SLIDE_DOWN:
        return "Slide Down";
      case Transition::PUSH_LEFT:
        return "Push Left";
      case Transition::PUSH_RIGHT:
        return "Push Right";
      case Transition::PUSH_UP:
        return "Push Up";
      case Transition::PUSH_DOWN:
        return "Push Down";
      case Transition::AUTO_ANIMATE:
        return "Auto-Animate";
      default:
        return "None";
    }
  }

  QString to_label(Easing easing) {
    switch(easing) {
      case Easing::EASE_IN:
        return "Ease In";
      case Easing::EASE_IN_OUT:
        return "Ease In-Out";
      case Easing::LINEAR:
        return "None";
      case Easing::SNAP:
        return "Snap";
      case Easing::WIND_UP:
        return "Wind Up";
      case Easing::BOUNCE:
        return "Bounce";
      default:
        return "Ease Out";
    }
  }

  QString format_seconds(double seconds) {
    return QString::number(seconds, 'g', 6) + " s";
  }

  std::optional<double> parse_seconds(QString text) {
    text = text.trimmed();
    if(text.endsWith('s')) {
      text.chop(1);
    }
    auto is_valid = false;
    auto value = text.trimmed().toDouble(&is_valid);
    if(!is_valid || value < 0) {
      return std::nullopt;
    }
    return value;
  }

  QLabel* make_label(const QString& text, const char* name = "field_label") {
    auto label = new QLabel(text);
    label->setObjectName(name);
    return label;
  }

  Section make_section(const QString& title) {
    auto widget = new QWidget();
    widget->setObjectName("inspector_section");
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(SIDE_MARGIN, 14, SIDE_MARGIN, 16);
    layout->setSpacing(12);
    auto row = new QWidget();
    auto header = new QHBoxLayout(row);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    header->addWidget(make_label(title.toUpper(), "section_heading"));
    auto info = new QLabel();
    info->setPixmap(get_icon(Glyph::INFO).pixmap(QSize(14, 14)));
    info->setToolTip(title);
    header->addWidget(info);
    header->addStretch(1);
    row->setFixedHeight(22);
    layout->addWidget(row);
    return Section(widget, layout, header);
  }

  QFrame* make_rule() {
    auto rule = new QFrame();
    rule->setObjectName("section_rule");
    rule->setFixedHeight(1);
    return rule;
  }

  QComboBox* make_combo(const std::vector<Option>& options, int current) {
    auto box = new QComboBox();
    box->setObjectName("choice_field");
    for(auto& option : options) {
      box->addItem(option.m_label);
    }
    if(auto model = qobject_cast<QStandardItemModel*>(box->model())) {
      for(auto i = 0; i != static_cast<int>(options.size()); ++i) {
        if(!options[i].m_is_enabled) {
          model->item(i)->setEnabled(false);
        }
      }
    }
    box->setCurrentIndex(current);
    return box;
  }

  QComboBox* make_seconds_combo(
      const std::array<double, 7>& presets, double value) {
    auto box = new QComboBox();
    box->setObjectName("choice_field");
    box->setEditable(true);
    box->setInsertPolicy(QComboBox::NoInsert);
    for(auto preset : presets) {
      box->addItem(format_seconds(preset));
    }
    box->setEditText(format_seconds(value));
    return box;
  }

  QWidget* make_field(const QString& label, QWidget* input) {
    auto widget = new QWidget();
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(make_label(label));
    layout->addWidget(input);
    return widget;
  }

  class KeyField : public QLineEdit {
    public:
      std::function<void (const KeyChord&)> m_handler;

    protected:
      void keyPressEvent(QKeyEvent* event) override {
        auto chord = make_key_chord(event->key(), event->modifiers(),
          event->text().toStdU16String());
        if(chord.m_key.empty()) {
          event->accept();
          return;
        }
        setText(QString::fromStdString(to_string(chord)));
        if(m_handler) {
          m_handler(chord);
        }
        event->accept();
      }
  };

  struct StateChoice {
    StateId m_id;
    QString m_name;
  };

  const ComponentInstance* get_instance(const Node& node) {
    if(auto group = node.get_group(); group && group->m_component_instance) {
      return &*group->m_component_instance;
    }
    return nullptr;
  }

  std::vector<StateChoice> find_states(
      const Document& document, const NodeId& id) {
    auto current = id;
    auto owner = NodeId();
    for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node) {
        break;
      }
      if(get_instance(*node)) {
        owner = current;
        break;
      }
      if(node->m_parent.is_nil() && !document.is_on_canvas(current)) {
        auto root = current;
        document.get_nodes().for_each([&] (const auto& candidate) {
          if(auto instance = get_instance(candidate)) {
            for(auto& state : instance->m_states) {
              if(state.m_tree == root) {
                owner = candidate.get_id();
              }
            }
          }
        });
        break;
      }
      current = node->m_parent;
    }
    auto choices = std::vector<StateChoice>();
    if(owner.is_nil()) {
      return choices;
    }
    auto& instance = *get_instance(document.get(owner));
    auto original = instance.m_state;
    if(original.is_nil()) {
      original = instance.m_component;
      auto& components = document.get_resources().m_components;
      if(auto component = components.find(instance.m_component);
          component != components.end() &&
          !component->second.m_default_state.is_nil()) {
        original = component->second.m_default_state;
      }
    }
    choices.push_back(StateChoice(original, "Default State"));
    auto add = [&] (const ComponentState& state) {
      if(state.m_id == original) {
        choices.front().m_name = QString::fromStdString(state.m_name);
        return;
      }
      if(std::ranges::any_of(choices, [&] (const auto& choice) {
          return choice.m_id == state.m_id;
        })) {
        return;
      }
      choices.push_back(
        StateChoice(state.m_id, QString::fromStdString(state.m_name)));
    };
    for(auto& state : instance.m_states) {
      add(state);
    }
    auto& components = document.get_resources().m_components;
    if(auto component = components.find(instance.m_component);
        component != components.end()) {
      for(auto& state : component->second.m_states) {
        add(state);
      }
    }
    return choices;
  }

  int get_trigger_index(const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return 1;
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return 2;
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return 3;
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return 4;
    }
    return 0;
  }

  int get_type_index(const Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      if(transition->m_animation.m_transition == Transition::AUTO_ANIMATE) {
        return 1;
      }
      return 0;
    } else if(std::holds_alternative<Overlay>(action)) {
      return 2;
    } else if(std::holds_alternative<PreviousArtboard>(action)) {
      return 4;
    } else if(std::holds_alternative<StateTransition>(action)) {
      return 5;
    } else if(std::holds_alternative<VideoPlayback>(action)) {
      return 8;
    }
    return 0;
  }

  QString summarize(const Interaction& interaction) {
    auto& triggers = PrototypeInspector::get_trigger_labels();
    auto& types = PrototypeInspector::get_type_labels();
    return QString::fromStdString(
      triggers[get_trigger_index(interaction.m_trigger)] + " - " +
      types[get_type_index(interaction.m_action)]);
  }

  Animation* get_animation(Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return &transition->m_animation;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return &overlay->m_animation;
    } else if(auto state = std::get_if<StateTransition>(&action)) {
      return &state->m_animation;
    }
    return nullptr;
  }

  NodeId get_destination(const Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return overlay->m_destination;
    }
    return NodeId();
  }
}

const std::vector<std::string>& PrototypeInspector::get_trigger_labels() {
  static const auto LABELS = std::vector<std::string>(
    {"Tap", "Drag", "Time", "Hover", "Keys & Gamepad", "Voice"});
  return LABELS;
}

const std::vector<std::string>& PrototypeInspector::get_type_labels() {
  static const auto LABELS = std::vector<std::string>({"Transition",
    "Auto-Animate", "Overlay", "Scroll To", "Previous Artboard",
    "Transition to State", "Audio Playback", "Speech Playback",
    "Video Playback"});
  return LABELS;
}

PrototypeInspector::PrototypeInspector(QWidget* parent)
    : QWidget(parent),
      m_editor(nullptr),
      m_is_pending(false),
      m_scroll(new QScrollArea()),
      m_content(nullptr),
      m_layout(nullptr) {
  setObjectName("prototype_inspector");
  m_scroll->setObjectName("inspector_scroll");
  m_scroll->setFrameShape(QFrame::NoFrame);
  m_scroll->setWidgetResizable(true);
  m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_scroll);
  rebuild();
}

PrototypeInspector::~PrototypeInspector() = default;

Editor* PrototypeInspector::get_editor() const {
  return m_editor;
}

void PrototypeInspector::set_editor(Editor* editor) {
  m_subscription.release();
  m_selection_subscription.release();
  m_editor = editor;
  m_current = InteractionId();
  if(m_editor) {
    m_subscription =
      m_editor->get_document().subscribe([this] (const auto& changes) {
        schedule();
      });
    m_selection_subscription = m_editor->get_selection().subscribe([this] {
      schedule();
    });
  }
  refresh();
}

const NodeId& PrototypeInspector::get_node() const {
  return m_node;
}

const InteractionId& PrototypeInspector::get_current() const {
  return m_current;
}

void PrototypeInspector::set_current(const InteractionId& id) {
  m_current = id;
  schedule();
}

std::vector<std::string> PrototypeInspector::get_fields() const {
  auto fields = std::vector<std::string>();
  for(auto& field : m_fields) {
    fields.push_back(field.first);
  }
  return fields;
}

QWidget* PrototypeInspector::find_field(std::string_view name) const {
  for(auto& field : m_fields) {
    if(field.first == name) {
      return field.second;
    }
  }
  return nullptr;
}

void PrototypeInspector::refresh() {
  rebuild();
}

void PrototypeInspector::schedule() {
  if(m_is_pending) {
    return;
  }
  m_is_pending = true;
  QTimer::singleShot(0, this, [this] {
    if(m_is_pending) {
      rebuild();
    }
  });
}

void PrototypeInspector::rebuild() {
  m_is_pending = false;
  auto scroll = m_scroll->verticalScrollBar()->value();
  m_fields.clear();
  if(auto old = m_scroll->takeWidget()) {
    old->deleteLater();
  }
  m_content = new QWidget();
  m_content->setObjectName("inspector_content");
  m_layout = new QVBoxLayout(m_content);
  m_layout->setContentsMargins(0, 0, 0, 0);
  m_layout->setSpacing(0);
  auto document = m_editor ? &m_editor->get_document() : nullptr;
  m_node = NodeId();
  if(m_editor && m_editor->get_selection().get_size() == 1) {
    m_node = m_editor->get_selection().get_primary();
  }
  auto interactions = std::vector<const Interaction*>();
  if(document && !m_node.is_nil()) {
    interactions = get_interactions(*document, m_node);
  }
  auto current = static_cast<const Interaction*>(nullptr);
  for(auto interaction : interactions) {
    if(interaction->m_id == m_current) {
      current = interaction;
    }
  }
  if(!current && !interactions.empty()) {
    current = interactions.front();
  }
  m_current = current ? current->m_id : InteractionId();
  auto add_field = [&] (QVBoxLayout& layout, const std::string& name,
      const QString& label, QWidget* input) {
    m_fields.emplace_back(name, input);
    if(label.isEmpty()) {
      layout.addWidget(input);
    } else {
      layout.addWidget(make_field(label, input));
    }
  };
  auto section = make_section("Interaction");
  auto add = new QToolButton();
  add->setObjectName("icon_toggle");
  add->setAutoRaise(true);
  add->setIcon(get_icon(Glyph::PLUS));
  add->setIconSize(QSize(20, 20));
  add->setToolTip("Add Interaction");
  add->setAccessibleName("Add Interaction");
  add->setEnabled(!m_node.is_nil());
  section.m_header->addWidget(add);
  m_fields.emplace_back("add", add);
  QObject::connect(add, &QToolButton::clicked, this, [this] {
    if(!m_editor || m_node.is_nil()) {
      return;
    }
    auto& document = m_editor->get_document();
    auto interaction = make_default_interaction(m_node, NodeId());
    if(auto node = document.find(m_node); node && node->get_artboard()) {
      interaction.m_trigger = TimeTrigger(DEFAULT_DELAY);
    }
    auto command = std::make_unique<AddInteractionCommand>(interaction);
    auto id = command->get_interaction().m_id;
    try {
      if(m_editor->execute(std::move(command))) {
        m_current = id;
      }
    } catch(const std::invalid_argument&) {}
    schedule();
  });
  if(interactions.size() > 1) {
    for(auto i = std::size_t(0); i != interactions.size(); ++i) {
      auto row = new QToolButton();
      row->setObjectName("text_button");
      row->setCheckable(true);
      row->setChecked(interactions[i] == current);
      row->setText(summarize(*interactions[i]));
      auto id = interactions[i]->m_id;
      QObject::connect(row, &QToolButton::clicked, this, [this, id] {
        set_current(id);
      });
      add_field(*section.m_layout, "interaction_" + std::to_string(i),
        QString(), row);
    }
  }
  if(!current) {
    auto placeholder = [&] (const std::string& name, const QString& label,
        const QString& value) {
      auto box = make_combo({Option(value, true)}, 0);
      box->setEnabled(false);
      add_field(*section.m_layout, name, label, box);
    };
    placeholder("trigger", "Trigger", "Tap");
    placeholder("type", "Type", "Transition");
    placeholder("destination", "Destination", "");
    placeholder("animation", "Animation", "None");
    placeholder("easing", "Easing", "Ease In");
    placeholder("duration", "Duration", "0.2 s");
  } else {
    auto& base = *current;
    auto is_artboard = static_cast<bool>(document->get(m_node).get_artboard());
    auto states = find_states(*document, m_node);
    auto triggers = std::vector<Option>();
    for(auto i = 0; i != 6; ++i) {
      auto is_enabled = i != 5;
      if(is_artboard) {
        is_enabled = is_enabled && (i == 2 || i == 4);
      } else if(i == 3) {
        is_enabled = !states.empty();
      }
      triggers.push_back(Option(QString::fromStdString(
        get_trigger_labels()[i]), is_enabled));
    }
    auto trigger = make_combo(triggers, get_trigger_index(base.m_trigger));
    QObject::connect(trigger, &QComboBox::activated, this,
      [this, base] (int index) {
        auto changed = base;
        if(index == 0) {
          changed.m_trigger = TapTrigger();
        } else if(index == 1) {
          changed.m_trigger = DragTrigger();
        } else if(index == 2) {
          changed.m_trigger = TimeTrigger(DEFAULT_DELAY);
        } else if(index == 3) {
          changed.m_trigger = HoverTrigger();
        } else if(index == 4) {
          changed.m_trigger = KeyTrigger();
        } else {
          return;
        }
        if(changed.m_trigger.index() != base.m_trigger.index()) {
          apply(std::move(changed));
        }
      });
    add_field(*section.m_layout, "trigger", "Trigger", trigger);
    if(auto time = std::get_if<TimeTrigger>(&base.m_trigger)) {
      auto delay = make_seconds_combo(std::to_array({0.0, 0.5, 1.0, 2.0, 3.0,
        5.0, 10.0}), time->m_delay);
      auto commit = [this, base, delay] {
        if(auto seconds = parse_seconds(delay->currentText())) {
          auto changed = base;
          changed.m_trigger = TimeTrigger(*seconds);
          apply(std::move(changed));
        }
      };
      QObject::connect(delay, &QComboBox::activated, this, commit);
      QObject::connect(
        delay->lineEdit(), &QLineEdit::editingFinished, this, commit);
      add_field(*section.m_layout, "delay", "Delay", delay);
    } else if(auto key = std::get_if<KeyTrigger>(&base.m_trigger)) {
      auto keys = new KeyField();
      keys->setObjectName("text_field");
      keys->setText(QString::fromStdString(key->m_keys));
      keys->setPlaceholderText("Press a key");
      keys->m_handler = [this, base] (const KeyChord& chord) {
        auto changed = base;
        std::get<KeyTrigger>(changed.m_trigger).m_keys = to_string(chord);
        apply(std::move(changed));
      };
      add_field(*section.m_layout, "keys", "Keys", keys);
      auto device = make_combo({Option("Keyboard", true),
        Option("Gamepad", true)}, key->m_device == KeyDevice::GAMEPAD);
      QObject::connect(device, &QComboBox::activated, this,
        [this, base] (int index) {
          auto changed = base;
          std::get<KeyTrigger>(changed.m_trigger).m_device =
            index == 1 ? KeyDevice::GAMEPAD : KeyDevice::KEYBOARD;
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "device", "Device", device);
    }
    section.m_layout->addWidget(make_rule());
    auto types = std::vector<Option>();
    for(auto i = 0; i != 9; ++i) {
      auto is_enabled = i != 3 && i != 6 && i != 7;
      if(i == 5) {
        is_enabled = !states.empty();
      }
      types.push_back(
        Option(QString::fromStdString(get_type_labels()[i]), is_enabled));
    }
    auto type = make_combo(types, get_type_index(base.m_action));
    QObject::connect(type, &QComboBox::activated, this,
      [this, base, states] (int index) {
        auto changed = base;
        auto destination = get_destination(base.m_action);
        auto animation = make_default_animation();
        if(auto previous = get_animation(changed.m_action)) {
          animation = *previous;
        }
        if(index == 0) {
          if(animation.m_transition == Transition::AUTO_ANIMATE) {
            animation.m_transition = Transition::DISSOLVE;
          }
          changed.m_action = ArtboardTransition(destination, animation);
        } else if(index == 1) {
          animation.m_transition = Transition::AUTO_ANIMATE;
          changed.m_action = ArtboardTransition(destination, animation);
        } else if(index == 2) {
          if(!std::ranges::contains(
              OVERLAY_TRANSITIONS, animation.m_transition)) {
            animation.m_transition = Transition::DISSOLVE;
          }
          changed.m_action =
            Overlay(destination, Point(0, 0), animation);
        } else if(index == 4) {
          changed.m_action = PreviousArtboard();
        } else if(index == 5 && !states.empty()) {
          if(!std::ranges::contains(
              STATE_TRANSITIONS, animation.m_transition)) {
            animation.m_transition = Transition::DISSOLVE;
          }
          auto target = states.size() > 1 ? states[1].m_id : states[0].m_id;
          changed.m_action = StateTransition(target, animation);
        } else if(index == 8) {
          changed.m_action = VideoPlayback(base.m_owner, VideoCommand::PLAY);
        } else {
          return;
        }
        apply(std::move(changed));
      });
    add_field(*section.m_layout, "type", "Type", type);
    auto action = base.m_action;
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      auto preserve = new QCheckBox("Preserve Scroll Position");
      preserve->setChecked(transition->m_preserve_scroll);
      QObject::connect(preserve, &QCheckBox::clicked, this,
        [this, base] (bool is_checked) {
          auto changed = base;
          std::get<ArtboardTransition>(changed.m_action).m_preserve_scroll =
            is_checked;
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "preserve_scroll", QString(), preserve);
    }
    if(std::holds_alternative<ArtboardTransition>(action) ||
        std::holds_alternative<Overlay>(action)) {
      auto artboards = std::vector<NodeId>({NodeId()});
      auto options = std::vector<Option>({Option("None", true)});
      auto selected = 0;
      auto destination = get_destination(action);
      for(auto& id : document->get_canvas()) {
        auto node = document->find(id);
        if(!node || !node->get_artboard()) {
          continue;
        }
        if(id == destination) {
          selected = static_cast<int>(artboards.size());
        }
        artboards.push_back(id);
        options.push_back(
          Option(QString::fromStdString(node->m_header.m_name), true));
      }
      auto box = make_combo(options, selected);
      QObject::connect(box, &QComboBox::activated, this,
        [this, base, artboards] (int index) {
          auto changed = base;
          auto& target = artboards[index];
          if(auto transition =
              std::get_if<ArtboardTransition>(&changed.m_action)) {
            transition->m_destination = target;
          } else if(auto overlay = std::get_if<Overlay>(&changed.m_action)) {
            overlay->m_destination = target;
          }
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "destination", "Destination", box);
    }
    if(auto state = std::get_if<StateTransition>(&action)) {
      auto options = std::vector<Option>();
      auto selected = -1;
      for(auto i = std::size_t(0); i != states.size(); ++i) {
        options.push_back(Option(states[i].m_name, true));
        if(states[i].m_id == state->m_state) {
          selected = static_cast<int>(i);
        }
      }
      auto box = make_combo(options, selected);
      QObject::connect(box, &QComboBox::activated, this,
        [this, base, states] (int index) {
          auto changed = base;
          std::get<StateTransition>(changed.m_action).m_state =
            states[index].m_id;
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "state", "State", box);
    }
    if(auto overlay = std::get_if<Overlay>(&action)) {
      auto row = new QWidget();
      auto row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(8);
      for(auto axis : {0, 1}) {
        auto field = new QLineEdit();
        field->setObjectName("number_field");
        field->setValidator(new QDoubleValidator(field));
        auto value = axis == 0 ? overlay->m_position.m_x :
          overlay->m_position.m_y;
        field->setText(QString::number(value, 'g', 10));
        QObject::connect(field, &QLineEdit::editingFinished, this,
          [this, base, field, axis] {
            auto is_valid = false;
            auto value = field->text().toDouble(&is_valid);
            if(!is_valid) {
              return;
            }
            auto changed = base;
            auto& position = std::get<Overlay>(changed.m_action).m_position;
            if(axis == 0) {
              position.m_x = value;
            } else {
              position.m_y = value;
            }
            apply(std::move(changed));
          });
        row_layout->addWidget(make_label(axis == 0 ? "X" : "Y"));
        row_layout->addWidget(field, 1);
        m_fields.emplace_back(axis == 0 ? "overlay_x" : "overlay_y", field);
      }
      section.m_layout->addWidget(make_field("Overlay Position", row));
    }
    if(auto video = std::get_if<VideoPlayback>(&action)) {
      auto box = make_combo({Option("Play", true), Option("Pause", true),
        Option("Stop", true)}, static_cast<int>(video->m_command));
      QObject::connect(box, &QComboBox::activated, this,
        [this, base] (int index) {
          auto changed = base;
          std::get<VideoPlayback>(changed.m_action).m_command =
            static_cast<VideoCommand>(index);
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "video_command", "Playback", box);
    }
    if(auto animation = get_animation(action)) {
      auto is_auto = animation->m_transition == Transition::AUTO_ANIMATE;
      auto is_transition = std::holds_alternative<ArtboardTransition>(action);
      if(!(is_auto && is_transition)) {
        auto choices = std::vector<Transition>(
          TRANSITIONS.begin(), TRANSITIONS.end());
        if(std::holds_alternative<Overlay>(action)) {
          choices.assign(
            OVERLAY_TRANSITIONS.begin(), OVERLAY_TRANSITIONS.end());
        } else if(std::holds_alternative<StateTransition>(action)) {
          choices.assign(STATE_TRANSITIONS.begin(), STATE_TRANSITIONS.end());
        }
        auto options = std::vector<Option>();
        auto selected = -1;
        for(auto i = std::size_t(0); i != choices.size(); ++i) {
          options.push_back(Option(to_label(choices[i]), true));
          if(choices[i] == animation->m_transition) {
            selected = static_cast<int>(i);
          }
        }
        auto box = make_combo(options, selected);
        QObject::connect(box, &QComboBox::activated, this,
          [this, base, choices] (int index) {
            auto changed = base;
            get_animation(changed.m_action)->m_transition = choices[index];
            apply(std::move(changed));
          });
        add_field(*section.m_layout, "animation", "Animation", box);
      }
      auto easing_count = is_auto ? EASINGS.size() : std::size_t(4);
      auto easing_index = std::ranges::find(EASINGS, animation->m_easing) -
        EASINGS.begin();
      if(static_cast<std::size_t>(easing_index) >= easing_count) {
        easing_count = EASINGS.size();
      }
      auto options = std::vector<Option>();
      for(auto i = std::size_t(0); i != easing_count; ++i) {
        options.push_back(Option(to_label(EASINGS[i]), true));
      }
      auto easing = make_combo(options, static_cast<int>(easing_index));
      QObject::connect(easing, &QComboBox::activated, this,
        [this, base] (int index) {
          auto changed = base;
          get_animation(changed.m_action)->m_easing = EASINGS[index];
          apply(std::move(changed));
        });
      add_field(*section.m_layout, "easing", "Easing", easing);
      auto duration = make_seconds_combo(std::to_array({0.2, 0.3, 0.4, 0.6,
        0.8, 1.0, 2.0}), animation->m_duration);
      auto commit = [this, base, duration] {
        if(auto seconds = parse_seconds(duration->currentText())) {
          auto changed = base;
          get_animation(changed.m_action)->m_duration = *seconds;
          apply(std::move(changed));
        }
      };
      QObject::connect(duration, &QComboBox::activated, this, commit);
      QObject::connect(
        duration->lineEdit(), &QLineEdit::editingFinished, this, commit);
      add_field(*section.m_layout, "duration", "Duration", duration);
    }
    auto remove = new QToolButton();
    remove->setObjectName("text_button");
    remove->setText("Delete Interaction");
    auto id = base.m_id;
    QObject::connect(remove, &QToolButton::clicked, this, [this, id] {
      if(!m_editor) {
        return;
      }
      try {
        m_editor->execute(std::make_unique<RemoveInteractionCommand>(id));
      } catch(const std::invalid_argument&) {}
      schedule();
    });
    add_field(*section.m_layout, "remove", QString(), remove);
  }
  m_layout->addWidget(section.m_widget);
  m_layout->addWidget(make_rule());
  if(document && !m_node.is_nil()) {
    if(document->get(m_node).get_artboard()) {
      auto flows = make_section("Flow");
      auto flow = find_flow(*document, m_node);
      auto start = new QCheckBox("Start of Flow");
      start->setChecked(flow);
      auto artboard = m_node;
      QObject::connect(start, &QCheckBox::clicked, this,
        [this, artboard] (bool is_checked) {
          if(!m_editor) {
            return;
          }
          try {
            if(is_checked) {
              m_editor->execute(
                std::make_unique<AddFlowCommand>(artboard, std::string()));
            } else {
              m_editor->execute(std::make_unique<RemoveFlowCommand>(artboard));
            }
          } catch(const std::invalid_argument&) {}
          schedule();
        });
      add_field(*flows.m_layout, "flow", QString(), start);
      if(flow) {
        auto name = new QLineEdit();
        name->setObjectName("text_field");
        name->setText(QString::fromStdString(flow->m_name));
        QObject::connect(name, &QLineEdit::editingFinished, this,
          [this, artboard, name] {
            if(!m_editor) {
              return;
            }
            try {
              m_editor->execute(std::make_unique<RenameFlowCommand>(
                artboard, name->text().toStdString()));
            } catch(const std::invalid_argument&) {}
          });
        add_field(*flows.m_layout, "flow_name", "Name", name);
      }
      m_layout->addWidget(flows.m_widget);
      m_layout->addWidget(make_rule());
    } else {
      auto scrolling = make_section("Scrolling");
      auto fixed = new QCheckBox("Fix Position When Scrolling");
      fixed->setChecked(is_fixed(*document, m_node));
      fixed->setEnabled(false);
      add_field(*scrolling.m_layout, "fixed", QString(), fixed);
      m_layout->addWidget(scrolling.m_widget);
      m_layout->addWidget(make_rule());
    }
  }
  m_layout->addStretch(1);
  m_scroll->setWidget(m_content);
  m_scroll->verticalScrollBar()->setValue(scroll);
}

void PrototypeInspector::apply(Interaction interaction) {
  if(!m_editor) {
    return;
  }
  try {
    m_editor->execute(
      std::make_unique<SetInteractionCommand>(std::move(interaction)));
  } catch(const std::invalid_argument&) {}
  schedule();
}
