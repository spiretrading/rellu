#ifndef RELLU_MODEL_PROTOTYPE_HPP
#define RELLU_MODEL_PROTOTYPE_HPP
#include <string>
#include <variant>
#include <vector>
#include "Rellu/Geometry/Equality.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Model/NodeId.hpp"

namespace Rellu {

  /** Triggers an interaction when the owner is tapped or clicked. */
  struct TapTrigger {
    bool operator ==(const TapTrigger&) const = default;
  };

  /** Triggers an interaction when the pointer enters the owner. */
  struct HoverTrigger {
    bool operator ==(const HoverTrigger&) const = default;
  };

  /** Triggers an interaction when the owner is dragged. */
  struct DragTrigger {
    bool operator ==(const DragTrigger&) const = default;
  };

  /** Triggers an interaction after a delay. */
  struct TimeTrigger {

    /** The delay in seconds. */
    double m_delay = 0;

    bool operator ==(const TimeTrigger& trigger) const;
  };

  /** Lists the devices a key trigger listens to. */
  enum class KeyDevice : unsigned char {

    /** A keyboard. */
    KEYBOARD,

    /** A gamepad. */
    GAMEPAD
  };

  /** Triggers an interaction when a key or gamepad button is pressed. */
  struct KeyTrigger {

    /** The key or button combination, as Adobe XD names it. */
    std::string m_keys;

    /** The device listened to. */
    KeyDevice m_device = KeyDevice::KEYBOARD;

    bool operator ==(const KeyTrigger&) const = default;
  };

  /** The event starting an interaction. */
  using Trigger = std::variant<
    TapTrigger, HoverTrigger, DragTrigger, TimeTrigger, KeyTrigger>;

  /** Lists the visual transitions between screens. */
  enum class Transition : unsigned char {
    NONE,
    DISSOLVE,
    SLIDE_LEFT,
    SLIDE_RIGHT,
    SLIDE_UP,
    SLIDE_DOWN,
    PUSH_LEFT,
    PUSH_RIGHT,
    PUSH_UP,
    PUSH_DOWN,
    AUTO_ANIMATE
  };

  /** Lists the timing curves of a transition. */
  enum class Easing : unsigned char {
    LINEAR,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
    WIND_UP,
    BOUNCE,
    SNAP
  };

  /** How a change of screen or state is animated. */
  struct Animation {

    /** The visual transition. */
    Transition m_transition = Transition::NONE;

    /** The timing curve. */
    Easing m_easing = Easing::EASE_OUT;

    /** The duration in seconds. */
    double m_duration = 0;

    bool operator ==(const Animation& animation) const;
  };

  /** Navigates to another artboard. */
  struct ArtboardTransition {

    /** The artboard navigated to. */
    NodeId m_destination;

    /** How the navigation is animated. */
    Animation m_animation;

    /** Whether the scroll position is kept. */
    bool m_preserve_scroll = false;

    bool operator ==(const ArtboardTransition&) const = default;
  };

  /** Shows an artboard over the current one. */
  struct Overlay {

    /** The artboard shown. */
    NodeId m_destination;

    /** The position of the overlay on the current artboard. */
    Point m_position = Point(0, 0);

    /** How the overlay is animated. */
    Animation m_animation;

    bool operator ==(const Overlay&) const = default;
  };

  /** Switches a component instance to another state. */
  struct StateTransition {

    /** The state switched to. */
    StateId m_state;

    /** How the switch is animated. */
    Animation m_animation;

    bool operator ==(const StateTransition&) const = default;
  };

  /** Navigates back to the previously shown artboard. */
  struct PreviousArtboard {
    bool operator ==(const PreviousArtboard&) const = default;
  };

  /** Lists the commands controlling video playback. */
  enum class VideoCommand : unsigned char {

    /** Starts playing. */
    PLAY,

    /** Pauses playing. */
    PAUSE,

    /** Stops playing and rewinds. */
    STOP
  };

  /** Controls the playback of a video. */
  struct VideoPlayback {

    /** The node playing the video. */
    NodeId m_target;

    /** The command issued. */
    VideoCommand m_command = VideoCommand::PLAY;

    bool operator ==(const VideoPlayback&) const = default;
  };

  /** Opens a web page. */
  struct Hyperlink {

    /** The address of the page. */
    std::string m_url;

    bool operator ==(const Hyperlink&) const = default;
  };

  /** The response to an interaction's trigger. */
  using Action = std::variant<ArtboardTransition, Overlay, StateTransition,
    PreviousArtboard, VideoPlayback, Hyperlink>;

  /** A prototype interaction attached to a node. */
  struct Interaction {

    /** The interaction's identifier. */
    InteractionId m_id;

    /** The node the interaction is attached to. */
    NodeId m_owner;

    /** The event starting the interaction. */
    Trigger m_trigger;

    /** The response to the trigger. */
    Action m_action;

    /** Whether the interaction is active. */
    bool m_is_enabled = true;

    bool operator ==(const Interaction&) const = default;
  };

  /** A named starting point of a prototype. */
  struct Flow {

    /** The artboard the flow starts on. */
    NodeId m_artboard;

    /** The flow's name. */
    std::string m_name;

    bool operator ==(const Flow&) const = default;
  };

  /** The interactive behaviour of a document. */
  struct Prototype {

    /** The interactions in order. */
    std::vector<Interaction> m_interactions;

    /** The artboard the prototype starts on, or nil if unset. */
    NodeId m_home_artboard;

    /** The named flows. */
    std::vector<Flow> m_flows;

    bool operator ==(const Prototype&) const = default;
  };

  inline bool TimeTrigger::operator ==(const TimeTrigger& trigger) const {
    return equals(m_delay, trigger.m_delay);
  }

  inline bool Animation::operator ==(const Animation& animation) const {
    return m_transition == animation.m_transition &&
      m_easing == animation.m_easing &&
      equals(m_duration, animation.m_duration);
  }
}

#endif
