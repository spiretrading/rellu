#include <algorithm>
#include <array>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleValidator>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QRegularExpression>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QWheelEvent>
#include "Rellu/Commands/CompositeCommand.hpp"
#include "Rellu/Commands/Hierarchy.hpp"
#include "Rellu/Commands/TextCommands.hpp"
#include "Rellu/Model/Transaction.hpp"
#include "Rellu/Prototype/InteractionCommands.hpp"
#include "Rellu/Prototype/KeyNames.hpp"
#include "Rellu/Prototype/StageDocument.hpp"
#include "Rellu/Ui/FeatureCards.hpp"
#include "Rellu/Ui/PrototypeInspector.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "FieldKeys.hpp"
#include "HyperlinkStatus.hpp"
#include "Icons.hpp"

using namespace Rellu;

namespace {
  constexpr auto SIDE_MARGIN = 16;

  class ChoiceBox : public QComboBox {
    public:
      ChoiceBox() {
        setFocusPolicy(Qt::StrongFocus);
      }

    protected:
      void wheelEvent(QWheelEvent* event) override {
        if(hasFocus()) {
          QComboBox::wheelEvent(event);
        } else {
          event->ignore();
        }
      }
  };

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

  const auto STATE_TRANSITIONS =
    std::to_array({Transition::NONE, Transition::DISSOLVE});

  const auto EASINGS = std::to_array({Easing::LINEAR, Easing::EASE_OUT,
    Easing::EASE_IN, Easing::EASE_IN_OUT, Easing::SNAP, Easing::WIND_UP,
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
    return QString::number(seconds, 'g', 6) + translate("prototype", " s");
  }

  std::optional<double> parse_seconds(QString text) {
    text = text.trimmed();
    if(auto suffix = translate("prototype", " s").trimmed();
        text.endsWith(suffix)) {
      text.chop(suffix.size());
    } else if(text.endsWith('s')) {
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
    auto label = new QLabel(translate("prototype", text));
    label->setObjectName(name);
    return label;
  }

  Section make_section(const QString& title,
      std::optional<Feature> feature = std::nullopt) {
    auto widget = new QWidget();
    widget->setObjectName("inspector_section");
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(SIDE_MARGIN, 14, SIDE_MARGIN, 16);
    layout->setSpacing(12);
    auto row = new QWidget();
    auto header = new QHBoxLayout(row);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(6);
    auto heading = translate("prototype.section", title);
    header->addWidget(make_label(heading.toUpper(), "section_heading"));
    auto info = new QLabel();
    info->setPixmap(get_icon(Glyph::INFO).pixmap(QSize(14, 14)));
    info->setToolTip(heading);
    if(feature) {
      set_feature(*info, *feature);
    }
    header->addWidget(info);
    header->addStretch(1);
    row->setFixedHeight(22);
    layout->addWidget(row);
    return Section(widget, layout, header);
  }

  void set_field_tip(
      const std::string& name, const QString& label, QWidget& input) {
    if(name == "trigger" || name == "type" || name == "animation") {
      input.setToolTip(translate("prototype", label));
    }
  }

  QWidget* make_action_heading() {
    auto row = new QWidget();
    auto layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addWidget(make_label("ACTION", "section_heading"));
    auto info = new QLabel();
    info->setPixmap(get_icon(Glyph::INFO).pixmap(QSize(14, 14)));
    set_feature(*info, Feature::INTERACTION_ACTION_TYPE);
    layout->addWidget(info);
    layout->addStretch(1);
    return row;
  }

  QFrame* make_rule() {
    auto rule = new QFrame();
    rule->setObjectName("section_rule");
    rule->setFixedHeight(1);
    return rule;
  }

  QComboBox* make_combo(const std::vector<Option>& options, int current,
      std::string_view context = "prototype") {
    auto box = new ChoiceBox();
    box->setObjectName("choice_field");
    for(auto& option : options) {
      box->addItem(translate(context, option.m_label));
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
    auto box = new ChoiceBox();
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

  struct StateChoices {
    StateId m_current;
    std::vector<StateChoice> m_states;
  };

  StateChoices find_states(const Document& document, const NodeId& id) {
    auto found = find_component_states(document, id);
    auto choices = StateChoices();
    choices.m_current = found.m_current;
    for(auto& state : found.m_states) {
      auto name = QString::fromStdString(state.m_name);
      if(choices.m_states.empty()) {
        name = translate("prototype.destination", name);
      }
      choices.m_states.push_back(StateChoice(state.m_id, name));
    }
    return choices;
  }

  constexpr auto TAP = 0;
  constexpr auto DRAG = 1;
  constexpr auto HOVER = 2;
  constexpr auto TIME = 3;
  constexpr auto KEYS = 4;
  constexpr auto VOICE = 5;

  int get_trigger_index(const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return DRAG;
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return HOVER;
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return TIME;
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return KEYS;
    }
    return TAP;
  }

  constexpr auto TRANSITION = 0;
  constexpr auto AUTO_ANIMATE = 1;
  constexpr auto HYPERLINK = 2;
  constexpr auto OVERLAY = 3;
  constexpr auto SCROLL_TO = 4;
  constexpr auto PREVIOUS = 5;
  constexpr auto AUDIO = 6;
  constexpr auto SPEECH = 7;
  constexpr auto VIDEO = 8;

  int get_type_index(const Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      if(transition->m_animation.m_transition == Transition::AUTO_ANIMATE) {
        return AUTO_ANIMATE;
      }
      return TRANSITION;
    } else if(auto state = std::get_if<StateTransition>(&action)) {
      if(state->m_animation.m_transition == Transition::AUTO_ANIMATE) {
        return AUTO_ANIMATE;
      }
      return TRANSITION;
    } else if(std::holds_alternative<Hyperlink>(action)) {
      return HYPERLINK;
    } else if(std::holds_alternative<Overlay>(action)) {
      return OVERLAY;
    } else if(std::holds_alternative<PreviousArtboard>(action)) {
      return PREVIOUS;
    } else if(std::holds_alternative<VideoPlayback>(action)) {
      return VIDEO;
    } else if(std::holds_alternative<ScrollTransition>(action)) {
      return SCROLL_TO;
    }
    return TRANSITION;
  }

  std::vector<int> get_triggers(bool is_artboard, bool has_states,
      const std::vector<int>& used, int current) {
    auto triggers = std::vector<int>();
    for(auto kind : {TAP, DRAG, HOVER, TIME}) {
      if((kind != HOVER || has_states) && (kind != TIME || is_artboard) &&
          !std::ranges::contains(used, kind)) {
        triggers.push_back(kind);
      }
    }
    triggers.push_back(KEYS);
    triggers.push_back(VOICE);
    if(!std::ranges::contains(triggers, current)) {
      triggers.insert(std::ranges::upper_bound(triggers, current), current);
    }
    return triggers;
  }

  std::vector<int> get_types(int current) {
    auto types = std::vector<int>({TRANSITION, AUTO_ANIMATE, HYPERLINK,
      OVERLAY, SCROLL_TO, PREVIOUS, AUDIO, SPEECH});
    if(!std::ranges::contains(types, current)) {
      types.insert(std::ranges::upper_bound(types, current), current);
    }
    return types;
  }

  Animation* get_animation(Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return &transition->m_animation;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return &overlay->m_animation;
    } else if(auto state = std::get_if<StateTransition>(&action)) {
      return &state->m_animation;
    } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      return &scroll->m_animation;
    }
    return nullptr;
  }

  const Animation* get_animation(const Action& action) {
    return get_animation(const_cast<Action&>(action));
  }

  QString get_layer_label(const std::string& name) {
    auto text = QString::fromStdString(name);
    auto limit = 30;
    if(auto end = text.indexOf(QRegularExpression("\\s*\\n"));
        end >= 0 && end <= 30) {
      limit = static_cast<int>(end);
    }
    auto points = text.toStdU32String();
    if(points.size() <= static_cast<std::size_t>(limit)) {
      return text;
    }
    return QString::fromStdU32String(points.substr(0, limit)) + "...";
  }

  NodeId get_destination(const Action& action) {
    if(auto transition = std::get_if<ArtboardTransition>(&action)) {
      return transition->m_destination;
    } else if(auto overlay = std::get_if<Overlay>(&action)) {
      return overlay->m_destination;
    } else if(auto state = std::get_if<StateTransition>(&action)) {
      return state->m_state;
    }
    return NodeId();
  }

  Glyph get_trigger_glyph(const Trigger& trigger) {
    if(std::holds_alternative<DragTrigger>(trigger)) {
      return Glyph::TRIGGER_DRAG;
    } else if(std::holds_alternative<HoverTrigger>(trigger)) {
      return Glyph::TRIGGER_HOVER;
    } else if(std::holds_alternative<TimeTrigger>(trigger)) {
      return Glyph::TRIGGER_TIME;
    } else if(std::holds_alternative<KeyTrigger>(trigger)) {
      return Glyph::TRIGGER_KEYS;
    }
    return Glyph::TRIGGER_TAP;
  }

  Interaction change_type(const Interaction& base, int index) {
    auto changed = base;
    auto state = std::get_if<StateTransition>(&base.m_action);
    auto destination = state ? NodeId() : get_destination(base.m_action);
    auto animation = make_empty_animation();
    if(auto previous = get_animation(changed.m_action)) {
      animation = *previous;
    }
    if(index == TRANSITION || index == AUTO_ANIMATE) {
      if(index == AUTO_ANIMATE) {
        animation.m_transition = Transition::AUTO_ANIMATE;
      } else if(animation.m_transition == Transition::AUTO_ANIMATE) {
        animation.m_transition = Transition::DISSOLVE;
      }
      if(state) {
        changed.m_action = StateTransition(state->m_state, animation);
      } else {
        changed.m_action = ArtboardTransition(destination, animation);
      }
    } else if(index == HYPERLINK) {
      changed.m_action = Hyperlink();
    } else if(index == OVERLAY) {
      if(!std::ranges::contains(OVERLAY_TRANSITIONS, animation.m_transition)) {
        animation.m_transition =
          destination.is_nil() ? Transition::NONE : Transition::DISSOLVE;
      }
      changed.m_action = Overlay(destination, Point(0, 0), animation);
    } else if(index == SCROLL_TO) {
      animation.m_transition = Transition::DISSOLVE;
      changed.m_action = ScrollTransition(NodeId(), 0, animation);
    } else if(index == PREVIOUS) {
      changed.m_action = PreviousArtboard();
    } else if(index == VIDEO) {
      changed.m_action = VideoPlayback(base.m_owner, VideoCommand::PLAY);
    }
    return changed;
  }

  bool expects_destination(const Action& action) {
    return std::holds_alternative<ArtboardTransition>(action) ||
      std::holds_alternative<StateTransition>(action) ||
      std::holds_alternative<Overlay>(action) ||
      std::holds_alternative<ScrollTransition>(action);
  }

  QString get_mixed_text() {
    return QString(QChar(0x2014));
  }

  enum class DestinationKind : unsigned char {
    NONE,
    STATE,
    ARTBOARD,
    LAYER,
    SEPARATOR
  };

  struct DestinationTarget {
    DestinationKind m_kind;
    NodeId m_id;
    QString m_label;
  };

  struct DestinationChoices {
    std::vector<DestinationTarget> m_targets;
    NodeId m_current;
    QString m_placeholder;
  };

  bool is_state_trigger(const Trigger& trigger) {
    return std::holds_alternative<TapTrigger>(trigger) ||
      std::holds_alternative<HoverTrigger>(trigger) ||
      std::holds_alternative<KeyTrigger>(trigger);
  }

  int count_artboards(const Document& document) {
    auto count = 0;
    for(auto& id : document.get_canvas()) {
      if(auto node = document.find(id); node && node->get_artboard()) {
        ++count;
      }
    }
    return count;
  }

  DestinationChoices get_destination_choices(const Document& document,
      const NodeId& owner, const Interaction& interaction,
      const StateChoices& states) {
    auto& action = interaction.m_action;
    auto& trigger = interaction.m_trigger;
    auto choices = DestinationChoices();
    auto scroll = std::get_if<ScrollTransition>(&action);
    choices.m_current =
      scroll ? scroll->m_destination : get_destination(action);
    auto is_transition = std::holds_alternative<ArtboardTransition>(action) ||
      std::holds_alternative<StateTransition>(action);
    auto has_states = is_transition;
    auto has_artboards =
      is_transition || std::holds_alternative<Overlay>(action);
    auto has_layers = scroll != nullptr;
    if(count_artboards(document) == 1) {
      has_states = true;
      has_layers = true;
    }
    if(!is_state_trigger(trigger)) {
      has_states = false;
    }
    if(std::holds_alternative<HoverTrigger>(trigger)) {
      has_layers = false;
      has_artboards = false;
    }
    if(std::holds_alternative<DragTrigger>(trigger)) {
      has_layers = false;
    }
    auto state_targets = std::vector<DestinationTarget>();
    if(has_states && states.m_states.size() > 1) {
      for(auto& state : states.m_states) {
        if(state.m_id != states.m_current ||
            state.m_id == choices.m_current) {
          state_targets.push_back(DestinationTarget(
            DestinationKind::STATE, state.m_id, state.m_name));
        }
      }
    }
    auto artboard_targets = std::vector<DestinationTarget>();
    if(has_artboards) {
      auto own = find_artboard(document, owner);
      auto artboards = std::vector<const Node*>();
      for(auto& id : document.get_canvas()) {
        auto node = document.find(id);
        if(node && node->get_artboard() &&
            (id != own || id == choices.m_current)) {
          artboards.push_back(node);
        }
      }
      std::ranges::stable_sort(artboards, [] (auto left, auto right) {
        auto& first = left->get_artboard()->m_frame;
        auto& second = right->get_artboard()->m_frame;
        if(first.m_y != second.m_y) {
          return first.m_y < second.m_y;
        }
        return first.m_x < second.m_x;
      });
      for(auto node : artboards) {
        artboard_targets.push_back(DestinationTarget(DestinationKind::ARTBOARD,
          node->get_id(), QString::fromStdString(node->m_header.m_name)));
      }
    }
    auto layer_targets = std::vector<DestinationTarget>();
    if(has_layers) {
      for(auto& id : get_scroll_destinations(document, owner)) {
        layer_targets.push_back(DestinationTarget(DestinationKind::LAYER, id,
          get_layer_label(document.get(id).m_header.m_name)));
      }
    }
    auto& targets = choices.m_targets;
    if(!choices.m_current.is_nil()) {
      targets.push_back(DestinationTarget(DestinationKind::NONE, NodeId(),
        translate("prototype.destination", "None")));
    }
    for(auto section : {&state_targets, &artboard_targets, &layer_targets}) {
      if(section->empty()) {
        continue;
      }
      if(!targets.empty()) {
        targets.push_back(
          DestinationTarget(DestinationKind::SEPARATOR, NodeId(), QString()));
      }
      targets.insert(targets.end(), section->begin(), section->end());
    }
    auto is_state = !state_targets.empty();
    auto is_artboard = !artboard_targets.empty();
    auto is_layer = !layer_targets.empty();
    auto placeholder = [&] {
      if(is_state && is_artboard && is_layer) {
        return "(Choose a destination)";
      } else if(is_state && is_layer) {
        return "(Choose a state or a layer)";
      } else if(is_state && is_artboard) {
        return "(Choose an artboard or a state)";
      } else if(is_artboard && is_layer) {
        return "(Choose an artboard or a layer)";
      } else if(is_artboard) {
        return "(Choose an artboard)";
      } else if(is_state) {
        return "(Choose a state)";
      } else if(is_layer) {
        return "(Choose a layer)";
      }
      return "(Choose a destination)";
    }();
    choices.m_placeholder = translate("prototype", placeholder);
    return choices;
  }

  Interaction choose_destination(
      const Interaction& interaction, const DestinationTarget& target) {
    auto changed = interaction;
    auto& action = changed.m_action;
    auto animation = make_empty_animation();
    if(auto previous = get_animation(action)) {
      animation = *previous;
    }
    if(target.m_kind == DestinationKind::NONE) {
      if(auto transition = std::get_if<ArtboardTransition>(&action)) {
        transition->m_destination = NodeId();
      } else if(auto state = std::get_if<StateTransition>(&action)) {
        state->m_state = StateId();
      } else if(auto overlay = std::get_if<Overlay>(&action)) {
        overlay->m_destination = NodeId();
      } else if(auto scroll = std::get_if<ScrollTransition>(&action)) {
        scroll->m_destination = NodeId();
      }
    } else if(target.m_kind == DestinationKind::STATE) {
      if(animation.m_transition != Transition::NONE &&
          animation.m_transition != Transition::DISSOLVE &&
          animation.m_transition != Transition::AUTO_ANIMATE) {
        animation.m_transition = Transition::AUTO_ANIMATE;
      }
      action = StateTransition(target.m_id, animation);
    } else if(target.m_kind == DestinationKind::ARTBOARD) {
      if(auto overlay = std::get_if<Overlay>(&action)) {
        overlay->m_destination = target.m_id;
      } else {
        auto preserve = false;
        if(auto transition = std::get_if<ArtboardTransition>(&action)) {
          preserve = transition->m_preserve_scroll;
        }
        action = ArtboardTransition(target.m_id, animation, preserve);
      }
    } else if(target.m_kind == DestinationKind::LAYER) {
      if(auto scroll = std::get_if<ScrollTransition>(&action)) {
        scroll->m_destination = target.m_id;
      } else {
        animation.m_transition = Transition::DISSOLVE;
        action = ScrollTransition(target.m_id, 0, animation);
        if(!std::holds_alternative<TapTrigger>(changed.m_trigger) &&
            !std::holds_alternative<KeyTrigger>(changed.m_trigger)) {
          changed.m_trigger = TapTrigger();
        }
      }
    }
    return changed;
  }

  struct LinkRange {
    TextRange m_range;
    QString m_text;
    std::string m_url;
  };

  std::vector<LinkRange> find_links(const Text& text) {
    auto links = std::vector<LinkRange>();
    auto& content = *text.m_content;
    auto start = std::size_t(0);
    for(auto& run : get_runs(text)) {
      auto end = std::min(start + run.m_length, content.size());
      if(!run.m_hyperlink.empty()) {
        if(!links.empty() && links.back().m_range.m_end == start &&
            links.back().m_url == run.m_hyperlink) {
          links.back().m_range.m_end = end;
        } else {
          links.push_back(
            LinkRange(TextRange(start, end), QString(), run.m_hyperlink));
        }
      }
      start = end;
    }
    for(auto& link : links) {
      link.m_text = QString::fromStdU16String(content.substr(
        link.m_range.m_start, link.m_range.m_end - link.m_range.m_start));
    }
    return links;
  }
}

const std::vector<std::string>& PrototypeInspector::get_trigger_labels() {
  static const auto LABELS = std::vector<std::string>(
    {"Tap", "Drag", "Hover", "Time", "Keys & Gamepad", "Voice"});
  return LABELS;
}

const std::vector<std::string>& PrototypeInspector::get_type_labels() {
  static const auto LABELS = std::vector<std::string>({"Transition",
    "Auto-Animate", "Hyperlink", "Overlay", "Scroll To", "Previous Artboard",
    "Audio Playback", "Speech Playback", "Video Playback"});
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

PrototypeInspector::~PrototypeInspector() {
  discard_preview();
}

Editor* PrototypeInspector::get_editor() const {
  return m_editor;
}

void PrototypeInspector::set_editor(Editor* editor) {
  m_subscription.release();
  m_selection_subscription.release();
  m_context_subscription.release();
  discard_preview();
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
    m_context_subscription = m_editor->get_context().subscribe([this] {
      auto& edit = m_editor->get_context();
      if(edit.get_interactions() != m_selected ||
          edit.get_interaction_owner() != m_node ||
          edit.get_pending() != m_pending) {
        schedule();
      }
    });
  }
  refresh();
}

void PrototypeInspector::set_fixed_position_factory(
    FixedPositionFactory factory) {
  m_fixed_position_factory = std::move(factory);
  schedule();
}

void PrototypeInspector::set_release_handler(ReleaseHandler handler) {
  m_release_handler = std::move(handler);
}

const NodeId& PrototypeInspector::get_node() const {
  return m_node;
}

const InteractionId& PrototypeInspector::get_current() const {
  return m_current;
}

void PrototypeInspector::set_current(const InteractionId& id) {
  if(m_editor) {
    m_editor->get_context().set_interactions(m_node, {id});
  }
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
    if(!m_is_pending) {
      return;
    } else if(m_preview) {
      m_is_pending = false;
    } else {
      rebuild();
    }
  });
}

void PrototypeInspector::rebuild() {
  m_is_pending = false;
  if(auto focus = QApplication::focusWidget();
      focus && m_content && m_content->isAncestorOf(focus)) {
    focus->clearFocus();
    release();
  }
  discard_preview();
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
  if(m_editor) {
    auto pending = m_editor->get_context().get_pending();
    std::erase_if(pending, [&] (const auto& interaction) {
      return std::ranges::contains(document->get_prototype().m_interactions,
        interaction.m_id, &Interaction::m_id);
    });
    m_editor->get_context().set_pending(pending);
    m_pending = std::move(pending);
  }
  auto selected = std::vector<const Interaction*>();
  if(m_editor && !m_node.is_nil()) {
    auto& edit = m_editor->get_context();
    if(edit.get_interaction_owner() == m_node) {
      for(auto& id : edit.get_interactions()) {
        for(auto& interaction : document->get_prototype().m_interactions) {
          if(interaction.m_id == id) {
            selected.push_back(&interaction);
          }
        }
        for(auto& interaction : m_pending) {
          if(interaction.m_id == id) {
            selected.push_back(&interaction);
          }
        }
      }
    }
    if(selected.empty() && (edit.get_interaction_owner() != m_node ||
        !edit.get_interactions().empty())) {
      auto chosen = find_default_interaction(*document, m_node);
      for(auto interaction : interactions) {
        if(interaction->m_id == chosen) {
          selected.push_back(interaction);
        }
      }
    }
  }
  m_selected.clear();
  for(auto interaction : selected) {
    m_selected.push_back(interaction->m_id);
  }
  m_current = selected.empty() ? InteractionId() : selected.front()->m_id;
  if(m_editor) {
    m_editor->get_context().set_interactions(m_node, m_selected);
  }
  auto current =
    selected.size() == 1 ? selected.front() : static_cast<const Interaction*>(
      nullptr);
  auto add_field = [&] (QVBoxLayout& layout, const std::string& name,
      const QString& label, QWidget* input) {
    set_field_tip(name, label, *input);
    m_fields.emplace_back(name, input);
    if(label.isEmpty()) {
      layout.addWidget(input);
    } else {
      layout.addWidget(make_field(label, input));
    }
  };
  auto section = make_section("Interaction", Feature::INTERACTION);
  auto add = new QToolButton();
  add->setObjectName("icon_toggle");
  add->setAutoRaise(true);
  add->setIcon(get_icon(Glyph::PLUS));
  add->setIconSize(QSize(20, 20));
  add->setToolTip(translate("prototype", "Add Interaction"));
  add->setAccessibleName(add->toolTip());
  add->setEnabled(!m_node.is_nil());
  section.m_header->addWidget(add);
  m_fields.emplace_back("add", add);
  QObject::connect(add, &QToolButton::clicked, this, [this] {
    if(!m_editor || m_node.is_nil()) {
      return;
    }
    auto& document = m_editor->get_document();
    auto interaction = make_new_interaction(document, m_node,
      find_states(document, m_node).m_states.size() > 1,
      m_editor->get_context().get_last_edited() ?
        &*m_editor->get_context().get_last_edited() : nullptr);
    auto command = std::make_unique<AddInteractionCommand>(interaction);
    auto id = command->get_interaction().m_id;
    auto& edit = m_editor->get_context();
    if(!is_complete(document, command->get_interaction())) {
      auto pending = command->get_interaction();
      edit.set_interactions(m_node, {id});
      edit.set_pending({std::move(pending)});
      schedule();
      return;
    }
    auto commands = std::vector<std::unique_ptr<Command>>();
    commands.push_back(std::move(command));
    try {
      m_editor->execute(std::make_unique<CompositeCommand>(
        "Create Wire", std::move(commands)));
    } catch(const std::invalid_argument&) {}
    if(std::ranges::any_of(document.get_prototype().m_interactions,
        [&] (const auto& interaction) {
          return interaction.m_id == id;
        })) {
      edit.set_interactions(m_node, {id});
      m_editor->update_selection();
    }
    schedule();
  });
  auto wire = std::vector<const Interaction*>();
  if(current) {
    wire = get_wire(*document, *current);
    if(!std::ranges::contains(wire, current)) {
      wire.push_back(current);
      sort_by_trigger(wire);
    }
  }
  if(wire.size() > 1) {
    auto labels = label_interactions(wire);
    for(auto i = std::size_t(0); i != wire.size(); ++i) {
      auto row = new QToolButton();
      row->setObjectName("text_button");
      row->setCheckable(true);
      row->setChecked(wire[i] == current);
      row->setText(QString::fromStdString(labels[i]));
      row->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      row->setIcon(get_icon(get_trigger_glyph(wire[i]->m_trigger)));
      auto id = wire[i]->m_id;
      QObject::connect(row, &QToolButton::clicked, this, [this, id] {
        set_current(id);
      });
      add_field(*section.m_layout, "interaction_" + std::to_string(i),
        QString(), row);
    }
  }
  if(selected.size() > 1) {
    build_mixed(selected, *section.m_layout);
  } else if(!current) {
    build_placeholder(*section.m_layout);
  } else {
    auto& base = *current;
    auto is_artboard = static_cast<bool>(document->get(m_node).get_artboard());
    auto states = find_states(*document, m_node);
    auto trigger_index = get_trigger_index(base.m_trigger);
    auto used = std::vector<int>();
    for(auto interaction : interactions) {
      if(interaction != current) {
        used.push_back(get_trigger_index(interaction->m_trigger));
      }
    }
    auto trigger_kinds = get_triggers(
      is_artboard, states.m_states.size() > 1, used, trigger_index);
    auto triggers = std::vector<Option>();
    for(auto kind : trigger_kinds) {
      triggers.push_back(Option(QString::fromStdString(
        get_trigger_labels()[kind]), kind != VOICE));
    }
    auto trigger = make_combo(triggers, static_cast<int>(
      std::ranges::find(trigger_kinds, trigger_index) -
        trigger_kinds.begin()));
    QObject::connect(trigger, &QComboBox::activated, this,
      [this, base, trigger_kinds] (int index) {
        auto chosen = Trigger();
        auto kind = trigger_kinds[index];
        if(kind == TAP) {
          chosen = TapTrigger();
        } else if(kind == DRAG) {
          chosen = DragTrigger();
        } else if(kind == TIME) {
          chosen = TimeTrigger(DEFAULT_DELAY);
        } else if(kind == HOVER) {
          chosen = HoverTrigger();
        } else if(kind == KEYS) {
          chosen = KeyTrigger();
        } else {
          return;
        }
        if(chosen.index() != base.m_trigger.index()) {
          apply(change_trigger(base, std::move(chosen)), "Trigger");
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
          apply(std::move(changed), "Delay");
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
      keys->setPlaceholderText(translate("prototype", "Press a key"));
      keys->m_handler = [this, base] (const KeyChord& chord) {
        auto changed = base;
        std::get<KeyTrigger>(changed.m_trigger).m_keys = to_string(chord);
        apply(std::move(changed), "Key");
      };
      add_field(*section.m_layout, "keys", "Key", keys);
      auto device = make_combo({Option("Keyboard", true),
        Option("Gamepad", true)}, key->m_device == KeyDevice::GAMEPAD);
      QObject::connect(device, &QComboBox::activated, this,
        [this, base] (int index) {
          auto changed = base;
          std::get<KeyTrigger>(changed.m_trigger).m_device =
            index == 1 ? KeyDevice::GAMEPAD : KeyDevice::KEYBOARD;
          apply(std::move(changed), "Key");
        });
      add_field(*section.m_layout, "device", "Device", device);
    }
    section.m_layout->addWidget(make_rule());
    section.m_layout->addWidget(make_action_heading());
    auto type_index = get_type_index(base.m_action);
    auto type_kinds = get_types(type_index);
    auto types = std::vector<Option>();
    auto can_scroll = !is_artboard &&
      (std::holds_alternative<TapTrigger>(base.m_trigger) ||
        std::holds_alternative<KeyTrigger>(base.m_trigger));
    for(auto kind : type_kinds) {
      types.push_back(Option(QString::fromStdString(get_type_labels()[kind]),
        (kind != SCROLL_TO || can_scroll || kind == type_index) &&
          kind != AUDIO && kind != SPEECH));
    }
    auto type = make_combo(types, static_cast<int>(
      std::ranges::find(type_kinds, type_index) - type_kinds.begin()));
    QObject::connect(type, &QComboBox::activated, this,
      [this, base, type_kinds] (int selected) {
        auto changed = change_type(base, type_kinds[selected]);
        if(changed != base) {
          apply(std::move(changed), "Action");
        }
      });
    add_field(*section.m_layout, "type", "Type", type);
    auto action = base.m_action;
    if(std::holds_alternative<PreviousArtboard>(action)) {
      auto note = make_label("Targeting a previous artboard automatically "
        "reverses the last used transition.");
      note->setWordWrap(true);
      add_field(*section.m_layout, "previous_note", QString(), note);
    }
    if(auto transition = std::get_if<ArtboardTransition>(&action);
        transition &&
        transition->m_animation.m_transition != Transition::AUTO_ANIMATE) {
      auto preserve = new QCheckBox(
        translate("prototype", "Preserve Scroll Position"));
      preserve->setChecked(transition->m_preserve_scroll);
      QObject::connect(preserve, &QCheckBox::clicked, this,
        [this, base] (bool is_checked) {
          auto changed = base;
          std::get<ArtboardTransition>(changed.m_action).m_preserve_scroll =
            is_checked;
          apply(std::move(changed), "Preserve Scroll Position");
        });
      add_field(*section.m_layout, "preserve_scroll", QString(), preserve);
    }
    if(std::holds_alternative<ArtboardTransition>(action) ||
        std::holds_alternative<StateTransition>(action) ||
        std::holds_alternative<Overlay>(action) ||
        std::holds_alternative<ScrollTransition>(action)) {
      auto choices =
        get_destination_choices(*document, m_node, base, states);
      auto box = new ChoiceBox();
      box->setObjectName("choice_field");
      auto selected = -1;
      for(auto& target : choices.m_targets) {
        if(target.m_kind == DestinationKind::SEPARATOR) {
          box->insertSeparator(box->count());
          continue;
        }
        if(target.m_kind != DestinationKind::NONE &&
            target.m_id == choices.m_current) {
          selected = box->count();
        }
        box->addItem(target.m_label);
      }
      box->setPlaceholderText(choices.m_placeholder);
      box->setCurrentIndex(selected);
      QObject::connect(box, &QComboBox::activated, this,
        [this, base, targets = choices.m_targets] (int index) {
          if(index < 0 || index >= static_cast<int>(targets.size()) ||
              targets[index].m_kind == DestinationKind::SEPARATOR) {
            return;
          }
          auto changed = choose_destination(base, targets[index]);
          if(changed != base) {
            apply(std::move(changed), "Target");
          }
        });
      add_field(*section.m_layout, "destination", "Destination", box);
    }
    if(auto scroll = std::get_if<ScrollTransition>(&action)) {
      auto offset = new QLineEdit();
      offset->setObjectName("number_field");
      auto validator = new QDoubleValidator(offset);
      validator->setLocale(QLocale::c());
      offset->setValidator(validator);
      offset->setText(QString::number(scroll->m_offset, 'g', 10));
      QObject::connect(offset, &QLineEdit::textEdited, this,
        [this, base] (const QString& text) {
          auto is_valid = false;
          auto value = text.toDouble(&is_valid);
          if(!is_valid) {
            return;
          }
          auto changed = base;
          auto& transition = std::get<ScrollTransition>(changed.m_action);
          transition.m_offset = clamp_scroll_offset(
            m_editor->get_document(), transition.m_destination, value);
          preview(base, std::move(changed));
        });
      QObject::connect(offset, &QLineEdit::editingFinished, this,
        [this, base, offset] {
          auto is_valid = false;
          auto value = offset->text().toDouble(&is_valid);
          if(!is_valid) {
            discard_preview();
            return;
          }
          auto changed = base;
          auto& transition = std::get<ScrollTransition>(changed.m_action);
          if(value != transition.m_offset) {
            value = clamp_scroll_offset(
              m_editor->get_document(), transition.m_destination, value);
          }
          if(value == transition.m_offset) {
            discard_preview();
            offset->setText(QString::number(value, 'g', 10));
            return;
          }
          transition.m_offset = value;
          apply(std::move(changed), "Y-Offset");
        });
      add_field(*section.m_layout, "y_offset", "Y-Offset", offset);
    }
    if(auto link = std::get_if<Hyperlink>(&action)) {
      auto url = new QLineEdit();
      url->setObjectName("text_field");
      url->setText(QString::fromStdString(link->m_url));
      url->setPlaceholderText("https://");
      QObject::connect(url, &QLineEdit::editingFinished, this,
        [this, base, url] {
          auto changed = base;
          auto& target = std::get<Hyperlink>(changed.m_action).m_url;
          auto text = url->text().trimmed().toStdString();
          if(target == text) {
            return;
          }
          target = std::move(text);
          apply(std::move(changed), "Edit Hyperlink URL");
        });
      add_field(*section.m_layout, "url", "URL", url);
    }
    if(auto overlay = std::get_if<Overlay>(&action)) {
      auto row = new QWidget();
      auto row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(8);
      for(auto axis : {0, 1}) {
        auto field = new QLineEdit();
        field->setObjectName("number_field");
        auto validator = new QDoubleValidator(field);
        validator->setLocale(QLocale::c());
        field->setValidator(validator);
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
            apply(std::move(changed), "Change Interaction");
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
          apply(std::move(changed), "Playback");
        });
      add_field(*section.m_layout, "video_command", "Playback", box);
    }
    if(auto animation = get_animation(action)) {
      auto is_auto = animation->m_transition == Transition::AUTO_ANIMATE;
      auto is_transition = !std::holds_alternative<Overlay>(action);
      if(!(is_auto && is_transition) &&
          !std::holds_alternative<ScrollTransition>(action)) {
        auto choices = std::vector<Transition>(
          TRANSITIONS.begin(), TRANSITIONS.end());
        if(std::holds_alternative<Overlay>(action)) {
          choices.assign(
            OVERLAY_TRANSITIONS.begin(), OVERLAY_TRANSITIONS.end());
        } else if(std::holds_alternative<StateTransition>(action)) {
          choices.assign(STATE_TRANSITIONS.begin(), STATE_TRANSITIONS.end());
        }
        if(!std::ranges::contains(choices, animation->m_transition)) {
          choices.push_back(animation->m_transition);
        }
        auto options = std::vector<Option>();
        auto selected = -1;
        for(auto i = std::size_t(0); i != choices.size(); ++i) {
          options.push_back(Option(to_label(choices[i]), true));
          if(choices[i] == animation->m_transition) {
            selected = static_cast<int>(i);
          }
        }
        auto box = make_combo(options, selected, "prototype.animation");
        box->setEnabled(!get_destination(action).is_nil());
        QObject::connect(box, &QComboBox::activated, this,
          [this, base, choices] (int index) {
            auto changed = base;
            get_animation(changed.m_action)->m_transition = choices[index];
            apply(std::move(changed), "Animation");
          });
        add_field(*section.m_layout, "animation", "Animation", box);
      }
      auto options = std::vector<Option>();
      for(auto choice : EASINGS) {
        options.push_back(Option(to_label(choice), true));
      }
      auto easing = make_combo(options, static_cast<int>(
        std::ranges::find(EASINGS, animation->m_easing) - EASINGS.begin()),
        "prototype.easing");
      auto is_timed = animation->m_transition != Transition::NONE;
      easing->setEnabled(is_timed);
      QObject::connect(easing, &QComboBox::activated, this,
        [this, base] (int index) {
          auto changed = base;
          get_animation(changed.m_action)->m_easing = EASINGS[index];
          apply(std::move(changed), "Easing");
        });
      add_field(*section.m_layout, "easing", "Easing", easing);
    }
    if(auto animation = get_animation(action);
        animation && !std::holds_alternative<DragTrigger>(base.m_trigger)) {
      auto is_timed = animation->m_transition != Transition::NONE;
      auto duration = make_seconds_combo(std::to_array({0.2, 0.3, 0.4, 0.6,
        0.8, 1.0, 2.0}), animation->m_duration);
      auto commit = [this, base, duration] {
        auto seconds = parse_seconds(duration->currentText());
        auto changed = base;
        auto animation = get_animation(changed.m_action);
        if(!seconds ||
            format_seconds(*seconds) == format_seconds(animation->m_duration)) {
          return;
        }
        animation->m_duration = to_stored_seconds(*seconds);
        apply(std::move(changed), "Duration");
      };
      duration->setEnabled(is_timed);
      QObject::connect(duration, &QComboBox::activated, this, commit);
      QObject::connect(
        duration->lineEdit(), &QLineEdit::editingFinished, this, commit);
      add_field(*section.m_layout, "duration", "Duration", duration);
    }
    if(std::holds_alternative<TapTrigger>(base.m_trigger) &&
        !std::holds_alternative<Hyperlink>(action) &&
        !std::holds_alternative<VideoPlayback>(action)) {
      section.m_layout->addWidget(make_rule());
      auto row = new QWidget();
      auto row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->addWidget(make_action_heading());
      row_layout->addStretch(1);
      auto secondary = new QToolButton();
      secondary->setObjectName("icon_toggle");
      secondary->setAutoRaise(true);
      secondary->setIcon(get_icon(Glyph::PLUS));
      secondary->setIconSize(QSize(20, 20));
      secondary->setToolTip(translate("prototype", "Add Action"));
      secondary->setAccessibleName(secondary->toolTip());
      secondary->setEnabled(false);
      row_layout->addWidget(secondary);
      m_fields.emplace_back("secondary_action", secondary);
      section.m_layout->addWidget(row);
    }
  }
  m_layout->addWidget(section.m_widget);
  m_layout->addWidget(make_rule());
  auto text = document && !m_node.is_nil() ?
    document->get(m_node).get_text() : nullptr;
  if(auto links = text ? find_links(*text) : std::vector<LinkRange>();
      !links.empty()) {
    auto hyperlinks = make_section("Text Hyperlinks");
    for(auto i = std::size_t(0); i != links.size(); ++i) {
      auto& link = links[i];
      auto suffix = std::to_string(i);
      auto quote = make_label("\"" + link.m_text + "\"");
      quote->setWordWrap(true);
      add_field(*hyperlinks.m_layout, "link_text_" + suffix, QString(), quote);
      auto row = new QWidget();
      auto row_layout = new QHBoxLayout(row);
      row_layout->setContentsMargins(0, 0, 0, 0);
      row_layout->setSpacing(6);
      auto url = new QLineEdit();
      url->setObjectName("text_field");
      url->setText(QString::fromStdString(link.m_url));
      url->setPlaceholderText(translate("prototype", "Insert URL here"));
      row_layout->addWidget(url, 1);
      auto status = add_hyperlink_status(row, *url);
      auto node = m_node;
      QObject::connect(url, &QLineEdit::editingFinished, this,
        [this, node, link, url] {
          auto text = url->text().trimmed().toStdString();
          if(!m_editor || text == link.m_url) {
            return;
          }
          auto edit = TextStyleEdit();
          edit.m_hyperlink = std::move(text);
          try {
            m_editor->execute(std::make_unique<SetTextStyleCommand>(
              node, link.m_range, std::move(edit)));
          } catch(const std::invalid_argument&) {}
          schedule();
        });
      m_fields.emplace_back("link_url_" + suffix, url);
      m_fields.emplace_back("link_status_" + suffix,
        status->findChild<QLabel*>("hyperlink_status"));
      hyperlinks.m_layout->addWidget(status);
    }
    m_layout->addWidget(hyperlinks.m_widget);
    m_layout->addWidget(make_rule());
  }
  if(document && !m_node.is_nil()) {
    if(document->get(m_node).get_artboard()) {
      auto flows = make_section("Flow");
      auto flow = find_flow(*document, m_node);
      auto start = new QCheckBox(translate("prototype", "Start of Flow"));
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
      auto fixed = new QCheckBox(
        translate("prototype", "Fix Position When Scrolling"));
      fixed->setChecked(is_fixed(*document, m_node));
      fixed->setEnabled(static_cast<bool>(m_fixed_position_factory));
      auto node = m_node;
      QObject::connect(fixed, &QCheckBox::clicked, this,
        [this, node] (bool is_checked) {
          if(!m_editor || !m_fixed_position_factory) {
            return;
          }
          if(auto command = m_fixed_position_factory(node, is_checked)) {
            try {
              m_editor->execute(std::move(command));
            } catch(const std::invalid_argument&) {}
          }
          schedule();
        });
      add_field(*scrolling.m_layout, "fixed", QString(), fixed);
      m_layout->addWidget(scrolling.m_widget);
      m_layout->addWidget(make_rule());
    }
  }
  m_layout->addStretch(1);
  bind_keys();
  m_scroll->setWidget(m_content);
  m_scroll->verticalScrollBar()->setValue(scroll);
}

void PrototypeInspector::build_placeholder(QVBoxLayout& layout) {
  auto add = [&] (const std::string& name, const QString& label,
      QWidget* input) {
    input->setEnabled(false);
    set_field_tip(name, label, *input);
    m_fields.emplace_back(name, input);
    if(label.isEmpty()) {
      layout.addWidget(input);
    } else {
      layout.addWidget(make_field(label, input));
    }
  };
  auto placeholder = [&] (const std::string& name, const QString& label,
      const QString& value) {
    add(name, label, make_combo({Option(value, true)}, 0, "prototype." + name));
  };
  placeholder("trigger", "Trigger", "Tap");
  layout.addWidget(make_rule());
  layout.addWidget(make_action_heading());
  placeholder("type", "Type", "Transition");
  add("preserve_scroll", QString(),
    new QCheckBox(translate("prototype", "Preserve Scroll Position")));
  if(m_node.is_nil()) {
    placeholder("destination", "Destination", "(Choose a destination)");
  } else {
    auto destination = make_combo({}, -1, "prototype.destination");
    destination->setPlaceholderText(get_mixed_text());
    add("destination", "Destination", destination);
  }
  placeholder("animation", "Animation", "None");
  placeholder("easing", "Easing", "Ease In");
  placeholder("duration", "Duration", format_seconds(PLACEHOLDER_DURATION));
}

void PrototypeInspector::build_mixed(
    const std::vector<const Interaction*>& selected, QVBoxLayout& layout) {
  auto& document = m_editor->get_document();
  auto& base = *selected.front();
  auto add = [&] (const std::string& name, const QString& label,
      QWidget* input) {
    set_field_tip(name, label, *input);
    m_fields.emplace_back(name, input);
    if(label.isEmpty()) {
      layout.addWidget(input);
    } else {
      layout.addWidget(make_field(label, input));
    }
  };
  auto is_all = [&] (const auto& predicate) {
    return std::ranges::all_of(selected, [&] (auto interaction) {
      return predicate(*interaction);
    });
  };
  auto is_same = [&] (const auto& property) {
    return std::ranges::all_of(selected, [&] (auto interaction) {
      return property(*interaction) == property(base);
    });
  };
  auto owners = std::vector<NodeId>();
  for(auto interaction : selected) {
    if(!std::ranges::contains(owners, interaction->m_owner)) {
      owners.push_back(interaction->m_owner);
    }
  }
  auto available = std::vector<int>();
  if(selected.size() == owners.size()) {
    for(auto kind : {TAP, DRAG, HOVER, TIME}) {
      auto is_available = std::ranges::all_of(owners, [&] (const auto& id) {
        auto node = document.find(id);
        return (kind != TIME || (node && node->get_artboard())) &&
          (kind != HOVER || find_states(document, id).m_states.size() > 1);
      }) && std::ranges::none_of(selected, [&] (auto interaction) {
        return std::ranges::any_of(
          get_interactions(document, interaction->m_owner),
          [&] (auto other) {
            return other != interaction &&
              get_trigger_index(other->m_trigger) == kind;
          });
      });
      if(is_available) {
        available.push_back(kind);
      }
    }
  }
  available.push_back(KEYS);
  available.push_back(VOICE);
  auto triggers = std::vector<Option>();
  for(auto kind : available) {
    triggers.push_back(Option(
      QString::fromStdString(get_trigger_labels()[kind]), kind != VOICE));
  }
  auto is_one_trigger = is_same([] (const Interaction& interaction) {
    return get_trigger_index(interaction.m_trigger);
  });
  auto shown = 0;
  if(is_one_trigger) {
    if(auto kind = std::ranges::find(
        available, get_trigger_index(base.m_trigger));
        kind != available.end()) {
      shown = static_cast<int>(kind - available.begin());
    }
  }
  auto trigger = make_combo(triggers, shown);
  QObject::connect(trigger, &QComboBox::activated, this,
    [this, available] (int index) {
      auto kind = available[index];
      auto chosen = Trigger();
      if(kind == DRAG) {
        chosen = DragTrigger();
      } else if(kind == HOVER) {
        chosen = HoverTrigger();
      } else if(kind == TIME) {
        chosen = TimeTrigger(DEFAULT_DELAY);
      } else if(kind == KEYS) {
        chosen = KeyTrigger();
      } else if(kind != TAP) {
        return;
      }
      apply_all("Trigger", [&] (Interaction interaction) {
        if(interaction.m_trigger.index() == chosen.index()) {
          return interaction;
        }
        return change_trigger(std::move(interaction), chosen);
      });
    });
  add("trigger", "Trigger", trigger);
  if(is_all([] (const Interaction& interaction) {
      return std::holds_alternative<TimeTrigger>(interaction.m_trigger);
    })) {
    auto is_shared = is_same([] (const Interaction& interaction) {
      return std::get<TimeTrigger>(interaction.m_trigger).m_delay;
    });
    auto delay = make_seconds_combo(std::to_array({0.0, 0.5, 1.0, 2.0, 3.0,
      5.0, 10.0}), std::get<TimeTrigger>(base.m_trigger).m_delay);
    if(!is_shared) {
      delay->setEditText(QString());
      delay->lineEdit()->setPlaceholderText(get_mixed_text());
    }
    auto commit = [this, delay] {
      if(auto seconds = parse_seconds(delay->currentText())) {
        apply_all("Delay", [&] (Interaction interaction) {
          interaction.m_trigger = TimeTrigger(*seconds);
          return interaction;
        });
      }
    };
    QObject::connect(delay, &QComboBox::activated, this, commit);
    QObject::connect(
      delay->lineEdit(), &QLineEdit::editingFinished, this, commit);
    add("delay", "Delay", delay);
  }
  layout.addWidget(make_rule());
  layout.addWidget(make_action_heading());
  auto type_index = get_type_index(base.m_action);
  auto is_one_type = is_same([] (const Interaction& interaction) {
    return get_type_index(interaction.m_action);
  });
  auto type_kinds = get_types(type_index);
  auto types = std::vector<Option>();
  for(auto kind : type_kinds) {
    types.push_back(Option(QString::fromStdString(get_type_labels()[kind]),
      kind != AUDIO && kind != SPEECH));
  }
  auto type = make_combo(types, is_one_type ? static_cast<int>(
    std::ranges::find(type_kinds, type_index) - type_kinds.begin()) : -1);
  type->setPlaceholderText(get_mixed_text());
  QObject::connect(type, &QComboBox::activated, this,
    [this, type_kinds] (int index) {
      apply_all("Action", [&] (Interaction interaction) {
        return change_type(interaction, type_kinds[index]);
      });
    });
  add("type", "Type", type);
  if(is_one_type && type_index == TRANSITION && is_all(
      [] (const Interaction& interaction) {
        return std::holds_alternative<ArtboardTransition>(
          interaction.m_action);
      })) {
    auto preserve =
      new QCheckBox(translate("prototype", "Preserve Scroll Position"));
    preserve->setChecked(is_all([] (const Interaction& interaction) {
      return std::get<ArtboardTransition>(
        interaction.m_action).m_preserve_scroll;
    }));
    QObject::connect(preserve, &QCheckBox::clicked, this,
      [this] (bool is_checked) {
        apply_all("Preserve Scroll Position", [&] (Interaction interaction) {
          std::get<ArtboardTransition>(
            interaction.m_action).m_preserve_scroll = is_checked;
          return interaction;
        });
      });
    add("preserve_scroll", QString(), preserve);
  }
  if(is_all([] (const Interaction& interaction) {
      return expects_destination(interaction.m_action);
    })) {
    auto states = find_states(document, m_node);
    auto choices = get_destination_choices(document, m_node, base, states);
    auto is_shared = is_same([] (const Interaction& interaction) {
      return get_destination(interaction.m_action);
    });
    auto box = new ChoiceBox();
    box->setObjectName("choice_field");
    auto current = -1;
    for(auto& target : choices.m_targets) {
      if(target.m_kind == DestinationKind::SEPARATOR) {
        box->insertSeparator(box->count());
        continue;
      }
      if(is_shared && target.m_kind != DestinationKind::NONE &&
          target.m_id == choices.m_current) {
        current = box->count();
      }
      box->addItem(target.m_label);
    }
    box->setPlaceholderText(get_mixed_text());
    box->setCurrentIndex(current);
    QObject::connect(box, &QComboBox::activated, this,
      [this, targets = choices.m_targets] (int index) {
        if(index < 0 || index >= static_cast<int>(targets.size()) ||
            targets[index].m_kind == DestinationKind::SEPARATOR) {
          return;
        }
        apply_all("Target", [&] (Interaction interaction) {
          return choose_destination(interaction, targets[index]);
        });
      });
    add("destination", "Destination", box);
    auto is_timed = is_all([] (const Interaction& interaction) {
      auto animation = get_animation(interaction.m_action);
      return animation && animation->m_transition != Transition::NONE;
    });
    auto has_animation = is_all([] (const Interaction& interaction) {
      return get_animation(interaction.m_action) != nullptr;
    });
    auto is_transitioned = has_animation &&
      std::ranges::none_of(selected, [] (auto interaction) {
        auto index = get_type_index(interaction->m_action);
        return index == AUTO_ANIMATE || index == SCROLL_TO;
      });
    if(is_transitioned) {
      auto choices = std::vector<Transition>(
        TRANSITIONS.begin(), TRANSITIONS.end());
      if(std::ranges::any_of(selected, [] (auto interaction) {
          return std::holds_alternative<StateTransition>(
            interaction->m_action);
        })) {
        choices.assign(STATE_TRANSITIONS.begin(), STATE_TRANSITIONS.end());
      } else if(std::ranges::any_of(selected, [] (auto interaction) {
          return std::holds_alternative<Overlay>(interaction->m_action);
        })) {
        choices.assign(
          OVERLAY_TRANSITIONS.begin(), OVERLAY_TRANSITIONS.end());
      }
      auto is_shared = is_same([] (const Interaction& interaction) {
        return get_animation(interaction.m_action)->m_transition;
      });
      auto options = std::vector<Option>();
      auto current = -1;
      for(auto i = std::size_t(0); i != choices.size(); ++i) {
        options.push_back(Option(to_label(choices[i]), true));
        if(is_shared &&
            choices[i] == get_animation(base.m_action)->m_transition) {
          current = static_cast<int>(i);
        }
      }
      auto animation = make_combo(options, current, "prototype.animation");
      animation->setPlaceholderText(get_mixed_text());
      QObject::connect(animation, &QComboBox::activated, this,
        [this, choices] (int index) {
          apply_all("Animation", [&] (Interaction interaction) {
            get_animation(interaction.m_action)->m_transition =
              choices[index];
            return interaction;
          });
        });
      add("animation", "Animation", animation);
    }
    if(has_animation) {
      auto options = std::vector<Option>();
      for(auto choice : EASINGS) {
        options.push_back(Option(to_label(choice), true));
      }
      auto is_shared = is_same([] (const Interaction& interaction) {
        return get_animation(interaction.m_action)->m_easing;
      });
      auto easing = make_combo(options, is_shared ? static_cast<int>(
        std::ranges::find(EASINGS, get_animation(base.m_action)->m_easing) -
          EASINGS.begin()) : -1, "prototype.easing");
      easing->setPlaceholderText(get_mixed_text());
      easing->setEnabled(is_timed);
      QObject::connect(easing, &QComboBox::activated, this,
        [this] (int index) {
          apply_all("Easing", [&] (Interaction interaction) {
            get_animation(interaction.m_action)->m_easing = EASINGS[index];
            return interaction;
          });
        });
      add("easing", "Easing", easing);
      if(std::ranges::none_of(selected, [] (auto interaction) {
          return std::holds_alternative<DragTrigger>(interaction->m_trigger);
        })) {
        auto is_shared = is_same([] (const Interaction& interaction) {
          return format_seconds(
            get_animation(interaction.m_action)->m_duration);
        });
        auto duration = make_seconds_combo(std::to_array({0.2, 0.3, 0.4, 0.6,
          0.8, 1.0, 2.0}), get_animation(base.m_action)->m_duration);
        if(!is_shared) {
          duration->setEditText(QString());
          duration->lineEdit()->setPlaceholderText(get_mixed_text());
        }
        duration->setEnabled(is_timed);
        auto commit = [this, duration] {
          if(auto seconds = parse_seconds(duration->currentText())) {
            apply_all("Duration", [&] (Interaction interaction) {
              get_animation(interaction.m_action)->m_duration =
                to_stored_seconds(*seconds);
              return interaction;
            });
          }
        };
        QObject::connect(duration, &QComboBox::activated, this, commit);
        QObject::connect(
          duration->lineEdit(), &QLineEdit::editingFinished, this, commit);
        add("duration", "Duration", duration);
      }
    }
  }
}

void PrototypeInspector::apply(
    Interaction interaction, const std::string& label) {
  if(!m_editor) {
    return;
  }
  discard_preview();
  if(!std::holds_alternative<PreviousArtboard>(interaction.m_action)) {
    m_editor->get_context().set_last_edited(interaction);
  }
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto pending = m_editor->get_context().get_pending();
  stage(std::move(interaction), commands, pending);
  commit(label, std::move(commands), std::move(pending));
}

void PrototypeInspector::commit(const std::string& label,
    std::vector<std::unique_ptr<Command>> commands,
    std::vector<Interaction> pending) {
  auto is_recorded = false;
  if(!commands.empty()) {
    try {
      is_recorded = m_editor->execute(std::make_unique<CompositeCommand>(
        label, std::move(commands))) != nullptr;
    } catch(const std::invalid_argument&) {}
  }
  m_editor->get_context().set_pending(std::move(pending));
  if(is_recorded) {
    m_editor->update_selection();
  }
  schedule();
}

void PrototypeInspector::stage(Interaction interaction,
    std::vector<std::unique_ptr<Command>>& commands,
    std::vector<Interaction>& pending) {
  auto& document = m_editor->get_document();
  auto entry = std::ranges::find(pending, interaction.m_id, &Interaction::m_id);
  auto is_stored = std::ranges::any_of(document.get_prototype().m_interactions,
    [&] (const auto& stored) {
      return stored.m_id == interaction.m_id;
    });
  if(is_complete(document, interaction)) {
    if(entry != pending.end()) {
      pending.erase(entry);
      commands.push_back(
        std::make_unique<AddInteractionCommand>(std::move(interaction)));
    } else if(is_stored) {
      commands.push_back(
        std::make_unique<SetInteractionCommand>(std::move(interaction)));
    }
    return;
  }
  if(is_stored) {
    commands.push_back(
      std::make_unique<RemoveInteractionCommand>(interaction.m_id));
  }
  if(entry != pending.end()) {
    *entry = std::move(interaction);
  } else {
    pending.push_back(std::move(interaction));
  }
}

void PrototypeInspector::apply_all(const std::string& label,
    const std::function<Interaction (Interaction)>& change) {
  if(!m_editor) {
    return;
  }
  discard_preview();
  auto commands = std::vector<std::unique_ptr<Command>>();
  auto pending = m_editor->get_context().get_pending();
  for(auto& id : m_selected) {
    auto original = std::optional<Interaction>();
    for(auto& interaction :
        m_editor->get_document().get_prototype().m_interactions) {
      if(interaction.m_id == id) {
        original = interaction;
      }
    }
    for(auto& interaction : m_editor->get_context().get_pending()) {
      if(interaction.m_id == id) {
        original = interaction;
      }
    }
    if(!original) {
      continue;
    }
    auto changed = change(*original);
    if(!std::holds_alternative<PreviousArtboard>(changed.m_action)) {
      m_editor->get_context().set_last_edited(changed);
    }
    if(changed != *original) {
      stage(std::move(changed), commands, pending);
    }
  }
  commit(label, std::move(commands), std::move(pending));
}

void PrototypeInspector::preview(
    const Interaction& base, Interaction interaction) {
  if(!m_editor) {
    return;
  }
  if(std::ranges::contains(m_editor->get_context().get_pending(),
      base.m_id, &Interaction::m_id)) {
    return;
  }
  if(!m_preview) {
    m_preview = base;
  }
  auto& document = m_editor->get_document();
  try {
    auto transaction = Transaction(document);
    SetInteractionCommand(std::move(interaction)).apply(document, transaction);
    transaction.commit_unrecorded();
  } catch(const std::invalid_argument&) {}
}

void PrototypeInspector::discard_preview() {
  if(!m_preview) {
    return;
  }
  auto base = std::move(*m_preview);
  m_preview = std::nullopt;
  if(m_editor) {
    auto& document = m_editor->get_document();
    try {
      auto transaction = Transaction(document);
      SetInteractionCommand(std::move(base)).apply(document, transaction);
      transaction.commit_unrecorded();
    } catch(const std::invalid_argument&) {}
  }
  schedule();
}

void PrototypeInspector::release() {
  if(m_release_handler) {
    m_release_handler();
  }
}

void PrototypeInspector::bind_keys() {
  for(auto& field : m_fields) {
    auto input = field.second;
    if(!input || dynamic_cast<KeyField*>(input)) {
      continue;
    }
    auto line = qobject_cast<QLineEdit*>(input);
    if(auto box = qobject_cast<QComboBox*>(input)) {
      QObject::connect(box, &QComboBox::activated, this, [this, box] {
        box->clearFocus();
        release();
      });
      line = box->lineEdit();
    }
    if(line) {
      bind_field_keys(*input, [this, line, text = line->text()] {
        discard_preview();
        line->setText(text);
      }, [this] {
        release();
      });
    }
  }
}
