#ifndef RELLU_TOOLS_TOOL_MANAGER_HPP
#define RELLU_TOOLS_TOOL_MANAGER_HPP
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include "Rellu/Commands/Signal.hpp"
#include "Rellu/Editor/Editor.hpp"
#include "Rellu/Geometry/Matrix.hpp"
#include "Rellu/Geometry/Size.hpp"
#include "Rellu/Model/Subscription.hpp"
#include "Rellu/Rendering/CanvasOverlay.hpp"
#include "Rellu/Tools/ArtboardTitles.hpp"
#include "Rellu/Tools/HitTester.hpp"
#include "Rellu/Tools/Tool.hpp"
#include "Rellu/Tools/ToolContext.hpp"

namespace Rellu {

  /**
   * Routes the canvas's pointer and key events to the active tool and
   * draws the overlay: the artboard grids, guides and titles, the active
   * tool's state and the snap guides. Modifier keys pressed or released
   * during a gesture replay the most recent pointer move, so that
   * constraints apply at once. The pointer's position is the history's,
   * and the history settles nodes onto or off artboards only once the left
   * button is released, as Adobe XD ends each drag's edit on release, and
   * keeps a node is_auto_animate_matched accepts in its artboard.
   * Tools are registered by name; the select,
   * rectangle, ellipse, polygon, line, pen, path_edit, artboard, zoom, text
   * and prototype tools are registered from the start.
   */
  class ToolManager {
    public:

      /**
       * Constructs a ToolManager whose active tool is the select tool,
       * measuring text by its estimated bounds.
       * @param editor The editing state, which must outlive this
       *        ToolManager.
       */
      explicit ToolManager(Editor& editor);

      /**
       * Constructs a ToolManager whose active tool is the select tool.
       * @param editor The editing state, which must outlive this
       *        ToolManager.
       * @param measurer The function hit testing and tools measure text
       *        with.
       */
      ToolManager(Editor& editor, TextMeasurer measurer);

      ~ToolManager();

      /** Returns the context tools work in. */
      ToolContext& get_context();

      /** Returns the context tools work in. */
      const ToolContext& get_context() const;

      /** Returns the hit tester. */
      HitTester& get_hit_tester();

      /** Returns the active tool. */
      Tool& get_tool();

      /**
       * Registers a tool, throwing std::invalid_argument if the name is
       * taken.
       * @param name The tool's name, such as text.
       * @param tool The tool.
       */
      void add_tool(std::string name, std::unique_ptr<Tool> tool);

      /** Returns <code>true</code> iff a tool has the given name. */
      bool has_tool(const std::string& name) const;

      /**
       * Makes a registered tool active, cancelling the gesture of the one
       * before, throwing std::out_of_range if no tool has the name.
       * @param name The tool's name.
       */
      void set_tool(const std::string& name);

      /** Returns the name of the active tool. */
      const std::string& get_tool_name() const;

      /**
       * Subscribes an observer to every change of the active tool.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe_tool(
        std::function<void ()> observer);

      /**
       * Sets the view of the canvas.
       * @param view The invertible transform from document space to logical
       *        units.
       * @param scale The number of pixels per logical unit.
       * @param size The size of the view in logical units.
       */
      void set_view(const Matrix& view, double scale, Size size);

      void press(const PointerEvent& event);
      void move(const PointerEvent& event);
      void release(const PointerEvent& event);
      void double_click(const PointerEvent& event);
      void leave();
      bool key_press(const KeyEvent& event);
      bool key_release(const KeyEvent& event);

      /** Abandons the active tool's gesture. */
      void cancel();

      /** Returns <code>true</code> iff a gesture is in progress. */
      bool is_busy() const;

      /** Returns the pointer to show. */
      ToolCursor get_cursor() const;

      /**
       * Returns the instance a component dragged over the canvas replaces
       * if dropped, or nil if there is none.
       */
      const NodeId& get_drop_target() const;

      /**
       * Sets the instance a component dragged over the canvas replaces if
       * dropped, which the overlay fills with translucent blue as Adobe XD
       * does.
       * @param target The instance, or nil for none.
       */
      void set_drop_target(const NodeId& target);

      /** Draws the overlay. */
      CanvasOverlay draw();

      /**
       * Subscribes an observer to every change that may change the overlay
       * or the pointer.
       * @param observer The function called after each change.
       * @return The handle ending the subscription when released.
       */
      [[nodiscard]] Subscription subscribe(std::function<void ()> observer);

    private:
      Editor* m_editor;
      HitTester m_hit_tester;
      ArtboardTitles m_titles;
      ToolContext m_context;
      std::unordered_map<std::string, std::unique_ptr<Tool>> m_tools;
      Tool* m_tool;
      std::string m_name;
      PointerEvent m_last_move;
      bool m_has_move;
      NodeId m_drop_target;
      Signal m_signal;
      Signal m_tool_signal;
      Subscription m_document_subscription;
      Subscription m_selection_subscription;
      Subscription m_context_subscription;

      ToolManager(const ToolManager&) = delete;
      ToolManager& operator =(const ToolManager&) = delete;
      bool replay(const KeyEvent& event);
      void take_request();
  };
}

#endif
