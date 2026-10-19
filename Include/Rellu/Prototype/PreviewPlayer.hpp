#ifndef RELLU_PROTOTYPE_PREVIEW_PLAYER_HPP
#define RELLU_PROTOTYPE_PREVIEW_PLAYER_HPP
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Geometry/Point.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Document.hpp"
#include "Rellu/Model/NodeId.hpp"
#include "Rellu/Model/Prototype.hpp"
#include "Rellu/Prototype/AutoAnimate.hpp"
#include "Rellu/Prototype/KeyNames.hpp"

namespace Rellu {

  /** An artboard drawn in the preview's viewport. */
  struct PreviewLayer {

    /** The artboard drawn. */
    NodeId m_artboard;

    /**
     * The artboard holding the artboard's layers fixed in place when it
     * scrolls, drawn over it without scrolling, or nil if there is none.
     */
    NodeId m_fixed;

    /** The position of the artboard's top left corner in the viewport. */
    Point m_position = Point(0, 0);

    /** The size of the part of the artboard shown. */
    Size m_size = Size(0, 0);

    /** The distance the artboard's content is scrolled up. */
    double m_scroll = 0;

    /** The opacity the artboard is drawn with. */
    double m_opacity = 1;

    bool operator ==(const PreviewLayer&) const = default;
  };

  /** An artboard shown over the current one. */
  struct PreviewOverlay {

    /** The artboard shown. */
    NodeId m_artboard;

    /** The position of the artboard's top left corner in the viewport. */
    Point m_position = Point(0, 0);

    /** How the overlay was shown, played in reverse to hide it. */
    Animation m_animation;

    bool operator ==(const PreviewOverlay&) const = default;
  };

  /** Where a preview is: the artboard shown, its scroll and its history. */
  struct PreviewPosition {

    /** The artboard shown. */
    NodeId m_artboard;

    /** The distance the artboard is scrolled. */
    double m_scroll = 0;

    /** The artboards navigated from, the most recent last. */
    std::vector<NodeId> m_history;
  };

  /**
   * Plays a document's prototype as Adobe XD's Desktop Preview does, in a
   * viewport the size of the current artboard's visible part, and keeps a
   * document of its own, the stage, showing what is to be drawn. Taps,
   * hovers, drags, keys and the passing of time trigger the interactions
   * of the layers shown, whose hit areas are their bounds, the topmost and
   * innermost first. Transitions slide, push or dissolve between artboards,
   * auto-animate interpolates the matched layers of two artboards or two
   * states, overlays show over the current artboard until a tap outside
   * them, previous returns through the history, and component instances
   * switch states, hover states reverting when the pointer leaves. Every
   * destination that does not resolve, and every action the preview does
   * not play, is reported.
   */
  class PreviewPlayer {
    public:

      /**
       * Finds the address of the hyperlink a text node shows at a point,
       * given the node and the point in its local coordinates, returning
       * an empty address if there is none.
       */
      using TextLinkFinder =
        std::function<std::string (const Node& node, Point position)>;

      /** The distance in viewport units a press moves before it drags. */
      static constexpr auto DRAG_THRESHOLD = 4.0;

      /**
       * Constructs a PreviewPlayer showing nothing.
       * @param stage The document to play, which the player changes as it
       *        plays, typically a copy of the document edited.
       * @param measurer The function measuring text.
       */
      explicit PreviewPlayer(std::unique_ptr<Document> stage,
        TextMeasurer measurer = estimate_text_bounds);

      ~PreviewPlayer();

      /** Returns the document played and drawn. */
      Document& get_stage() const;

      /**
       * Shows an artboard without a transition, clearing the history,
       * overlays and states switched.
       * @param artboard The artboard, which must be an artboard of the
       *        stage, otherwise std::invalid_argument is thrown.
       */
      void start(const NodeId& artboard);

      /**
       * Shows the artboard of a position, with its scroll and history, or
       * the first artboard if the position's artboard is gone.
       * @param position The position.
       */
      void start(const PreviewPosition& position);

      /** Returns the current position. */
      PreviewPosition get_position() const;

      /** Returns the artboard shown, or nil if there is none. */
      const NodeId& get_artboard() const;

      /**
       * Returns the size of the viewport: the width of the artboard shown
       * and the height of its visible part.
       */
      Size get_viewport() const;

      /** Returns the distance the artboard shown is scrolled. */
      double get_scroll() const;

      /** Returns the artboards navigated from, the most recent last. */
      std::vector<NodeId> get_history() const;

      /** Returns the overlays shown, bottom first. */
      const std::vector<PreviewOverlay>& get_overlays() const;

      /**
       * Returns the state a component instance shows, nil for the state it
       * shows in the document.
       * @param instance The instance.
       */
      StateId get_state(const NodeId& instance) const;

      /** Returns the artboards to draw, bottom first. */
      std::vector<PreviewLayer> get_layers() const;

      /** Returns the time in seconds since the preview started. */
      double get_time() const;

      /** Returns <code>true</code> iff an animation is playing. */
      bool is_animating() const;

      /**
       * Returns the time of the next timed trigger, or
       * <code>std::nullopt</code> if none is pending.
       */
      std::optional<double> get_next_time() const;

      /**
       * Advances the time, playing animations and firing timed triggers.
       * @param seconds The time elapsed.
       * @return <code>true</code> iff what is shown changed.
       */
      bool advance(double seconds);

      /**
       * Handles the press of the pointer.
       * @param position The position in the viewport.
       * @return <code>true</code> iff what is shown changed.
       */
      bool press(Point position);

      /**
       * Handles a move of the pointer, with or without the button held.
       * @param position The position in the viewport.
       * @return <code>true</code> iff what is shown changed.
       */
      bool move(Point position);

      /**
       * Handles the release of the pointer, tapping what it was pressed on
       * unless it dragged.
       * @param position The position in the viewport.
       * @return <code>true</code> iff what is shown changed.
       */
      bool release(Point position);

      /**
       * Handles the pointer leaving the viewport, reverting hover states.
       * @return <code>true</code> iff what is shown changed.
       */
      bool leave();

      /**
       * Scrolls the artboard shown.
       * @param delta The distance to scroll down.
       * @return <code>true</code> iff the scroll changed.
       */
      bool scroll(double delta);

      /**
       * Scrolls the innermost scroll group under a point that can scroll in
       * the direction asked for, or the artboard shown if there is none, as
       * the mouse wheel does.
       * @param position The position in the viewport.
       * @param delta The distance to scroll right and down.
       * @return <code>true</code> iff a scroll changed.
       */
      bool scroll(Point position, Point delta);

      /**
       * Returns the distance a scroll group's content is scrolled from where
       * the document places it.
       * @param group The scroll group.
       */
      Point get_scroll(const NodeId& group) const;

      /**
       * Sets the function opening the web pages of hyperlinks.
       * @param handler The function, given the page's address.
       */
      void set_link_handler(
        std::function<void (const std::string& url)> handler);

      /**
       * Sets the function finding the hyperlinks of text, which a tap
       * opens through the link handler instead of running tap
       * interactions.
       * @param finder The function.
       */
      void set_text_link_finder(TextLinkFinder finder);

      /**
       * Handles a key press, triggering the matching key trigger of the
       * topmost artboard shown.
       * @param chord The key pressed.
       * @return <code>true</code> iff an interaction was triggered.
       */
      bool press_key(const KeyChord& chord);

      /**
       * Performs an interaction's action as if its trigger fired.
       * @param id The interaction's id.
       * @return <code>true</code> iff the action did something.
       */
      bool trigger(const InteractionId& id);

      /** Returns the reports of what could not be played, in order. */
      const std::vector<std::string>& get_reports() const;

      /**
       * Finds the interaction of the topmost layer shown at a point whose
       * trigger is of the same kind as a given trigger.
       * @param position The position in the viewport.
       * @param trigger The trigger whose kind is looked for.
       * @return The enabled interaction, or <code>nullptr</code> if none.
       */
      const Interaction* find_interaction(
        Point position, const Trigger& trigger) const;

    private:
      struct Screen {
        NodeId m_artboard;
        double m_scroll;
        Animation m_animation;
      };
      struct InstanceState {
        StateId m_original;
        StateId m_current;
        std::unordered_map<StateId, NodeId> m_storage;
        bool m_is_cloned;
      };
      enum class Kind : unsigned char {
        SCREEN,
        OVERLAY,
        STATE
      };
      struct Animating {
        Kind m_kind;
        Animation m_animation;
        double m_start;
        bool m_is_reversed;
        Screen m_from;
        Screen m_to;
        std::optional<PreviewOverlay> m_overlay;
        std::unique_ptr<AutoAnimation> m_auto;
        NodeId m_discard;
      };
      struct Timer {
        InteractionId m_interaction;
        NodeId m_node;
        double m_time;
      };
      struct Hover {
        NodeId m_owner;
        NodeId m_instance;
        StateId m_state;
        Animation m_animation;
      };
      std::unique_ptr<Document> m_stage;
      TextMeasurer m_measurer;
      std::unordered_map<NodeId, std::vector<const Interaction*>> m_owners;
      std::unordered_map<InteractionId, const Interaction*> m_interactions;
      std::unordered_map<NodeId, NodeId> m_fixed_layers;
      std::unordered_map<NodeId, InstanceState> m_instances;
      std::unordered_map<NodeId, NodeId> m_state_roots;
      std::unordered_map<NodeId, NodeId> m_aliases;
      std::optional<Screen> m_screen;
      std::vector<Screen> m_history;
      std::vector<PreviewOverlay> m_overlays;
      std::vector<Animating> m_animations;
      std::vector<Timer> m_timers;
      std::vector<Hover> m_hovers;
      std::vector<std::string> m_reports;
      double m_time;
      std::optional<Point> m_press;
      bool m_is_dragging;
      std::optional<Point> m_drag_point;
      NodeId m_drag_group;
      std::unordered_map<NodeId, Point> m_scrolls;
      std::function<void (const std::string& url)> m_link_handler;
      TextLinkFinder m_text_link_finder;

      PreviewPlayer(const PreviewPlayer&) = delete;
      PreviewPlayer& operator =(const PreviewPlayer&) = delete;
      void prepare_stage();
      void report(std::string message);
      Size get_size(const NodeId& artboard) const;
      double get_maximum_scroll(const NodeId& artboard) const;
      std::vector<NodeId> get_roots() const;
      std::vector<const Interaction*> get_effective(const NodeId& id) const;
      void collect(const NodeId& root, std::vector<NodeId>& nodes) const;
      std::vector<std::pair<NodeId, Point>> get_targets(Point position) const;
      std::vector<std::pair<NodeId, const Interaction*>> find_all(
        Point position, std::size_t trigger) const;
      bool is_live(const Interaction& interaction) const;
      StateId get_original_state(const NodeId& instance) const;
      void finish_animations();
      void finish(Animating& animating);
      void schedule(const NodeId& root);
      void cancel_timers();
      bool perform(const Interaction& interaction, const NodeId& node);
      bool navigate(const NodeId& destination, const Animation& animation,
        bool is_scroll_preserved);
      bool go_back();
      bool show_overlay(const Overlay& overlay);
      bool hide_overlay();
      bool switch_state(const NodeId& instance, const StateId& state,
        const Animation& animation);
      NodeId find_instance(const NodeId& node, const StateId& state) const;
      NodeId find_tree(const NodeId& instance, const StateId& state,
        bool& is_shared) const;
      void discard(const NodeId& root);
      bool update_hover(Point position);
      std::vector<NodeId> find_scroll_groups(Point position) const;
      std::string find_text_link(Point position) const;
      bool scroll_group(const NodeId& group, Point delta);
      std::vector<PreviewLayer> get_screen_layers(const Screen& screen,
        Point position, double opacity) const;
  };
}

#endif
