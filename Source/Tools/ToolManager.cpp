#include <stdexcept>
#include <unordered_set>
#include <utility>
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Tools/ArtboardGrids.hpp"
#include "Rellu/Tools/ArtboardGuides.hpp"
#include "Rellu/Tools/ArtboardTool.hpp"
#include "Rellu/Tools/PathEditTool.hpp"
#include "Rellu/Tools/PenTool.hpp"
#include "Rellu/Tools/PrototypeTool.hpp"
#include "Rellu/Tools/SelectTool.hpp"
#include "Rellu/Tools/ShapeTool.hpp"
#include "Rellu/Tools/TextTool.hpp"
#include "Rellu/Tools/ToolManager.hpp"
#include "Rellu/Tools/ZoomTool.hpp"

using namespace Rellu;

namespace {
  bool is_modifier(int key) {
    return key == Qt::Key_Shift || key == Qt::Key_Alt ||
      key == Qt::Key_Control || key == Qt::Key_Meta;
  }
}

ToolManager::ToolManager(Editor& editor)
  : ToolManager(editor, estimate_text_bounds) {}

ToolManager::ToolManager(Editor& editor, TextMeasurer measurer)
    : m_editor(&editor),
      m_hit_tester(editor.get_document(), measurer),
      m_context(editor, m_hit_tester, m_titles),
      m_tool(nullptr),
      m_has_move(false) {
  m_context.set_text_measurer(std::move(measurer));
  add_tool("select", std::make_unique<SelectTool>());
  add_tool("rectangle", std::make_unique<ShapeTool>(ShapeKind::RECTANGLE));
  add_tool("ellipse", std::make_unique<ShapeTool>(ShapeKind::ELLIPSE));
  add_tool("polygon", std::make_unique<ShapeTool>(ShapeKind::POLYGON));
  add_tool("line", std::make_unique<ShapeTool>(ShapeKind::LINE));
  add_tool("pen", std::make_unique<PenTool>());
  add_tool("path_edit", std::make_unique<PathEditTool>());
  add_tool("artboard", std::make_unique<ArtboardTool>());
  add_tool("zoom", std::make_unique<ZoomTool>());
  add_tool("text", std::make_unique<TextTool>(get_default_font_library()));
  add_tool("prototype", std::make_unique<PrototypeTool>());
  m_tool = m_tools.at("select").get();
  m_name = "select";
  m_document_subscription =
    editor.get_document().subscribe([this] (const auto&) {
      m_context.update_titles();
      m_signal();
    });
  m_selection_subscription = editor.get_selection().subscribe([this] {
    m_signal();
  });
  m_context_subscription = editor.get_context().subscribe([this] {
    m_signal();
  });
}

ToolManager::~ToolManager() = default;

ToolContext& ToolManager::get_context() {
  return m_context;
}

const ToolContext& ToolManager::get_context() const {
  return m_context;
}

HitTester& ToolManager::get_hit_tester() {
  return m_hit_tester;
}

Tool& ToolManager::get_tool() {
  return *m_tool;
}

void ToolManager::add_tool(std::string name, std::unique_ptr<Tool> tool) {
  if(m_tools.contains(name)) {
    throw std::invalid_argument("Duplicate tool: " + name);
  }
  m_tools.emplace(std::move(name), std::move(tool));
}

bool ToolManager::has_tool(const std::string& name) const {
  return m_tools.contains(name);
}

void ToolManager::set_tool(const std::string& name) {
  auto entry = m_tools.find(name);
  if(entry == m_tools.end()) {
    throw std::out_of_range("Unknown tool: " + name);
  }
  if(name == m_name) {
    return;
  }
  m_tool->cancel(m_context);
  m_tool->leave(m_context);
  m_tool = entry->second.get();
  m_name = name;
  m_tool_signal();
  m_signal();
}

const std::string& ToolManager::get_tool_name() const {
  return m_name;
}

Subscription ToolManager::subscribe_tool(std::function<void ()> observer) {
  return m_tool_signal.subscribe(std::move(observer));
}

void ToolManager::set_view(const Matrix& view, double scale, Size size) {
  m_context.set_view(view, scale, size);
}

void ToolManager::press(const PointerEvent& event) {
  m_last_move = event;
  m_has_move = true;
  m_tool->press(m_context, event);
  take_request();
  m_signal();
}

void ToolManager::move(const PointerEvent& event) {
  m_last_move = event;
  m_has_move = true;
  m_tool->move(m_context, event);
  take_request();
  m_signal();
}

void ToolManager::release(const PointerEvent& event) {
  m_last_move = event;
  m_has_move = true;
  m_tool->release(m_context, event);
  take_request();
  m_signal();
}

void ToolManager::double_click(const PointerEvent& event) {
  m_last_move = event;
  m_has_move = true;
  m_tool->double_click(m_context, event);
  take_request();
  m_signal();
}

void ToolManager::leave() {
  m_has_move = false;
  m_tool->leave(m_context);
  m_signal();
}

bool ToolManager::key_press(const KeyEvent& event) {
  if(replay(event)) {
    return true;
  }
  auto is_used = m_tool->key_press(m_context, event);
  take_request();
  m_signal();
  return is_used;
}

bool ToolManager::key_release(const KeyEvent& event) {
  if(replay(event)) {
    return true;
  }
  auto is_used = m_tool->key_release(m_context, event);
  take_request();
  m_signal();
  return is_used;
}

void ToolManager::cancel() {
  m_tool->cancel(m_context);
  m_signal();
}

bool ToolManager::is_busy() const {
  return m_tool->is_busy();
}

ToolCursor ToolManager::get_cursor() const {
  return m_tool->get_cursor();
}

CanvasOverlay ToolManager::draw() {
  auto overlay = CanvasOverlay();
  auto highlights = std::unordered_set<NodeId>();
  auto& document = m_editor->get_document();
  for(auto& id : m_editor->get_selection().get_nodes()) {
    if(auto node = document.find(id); node && node->get_artboard()) {
      highlights.insert(id);
    }
  }
  auto& hover = m_editor->get_context().get_hover();
  if(auto node = document.find(hover); node && node->get_artboard()) {
    highlights.insert(hover);
  }
  draw_artboard_grids(
    document, m_context.get_view(), m_context.get_size(), overlay);
  draw_artboard_guides(m_context, overlay);
  m_titles.draw(overlay, m_context.get_scale(), highlights);
  m_tool->draw(m_context, overlay);
  m_context.get_snap_service().draw(m_context.get_view(), overlay);
  return overlay;
}

Subscription ToolManager::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

bool ToolManager::replay(const KeyEvent& event) {
  if(!is_modifier(event.m_key) || !m_has_move) {
    return false;
  }
  auto replayed = m_last_move;
  replayed.m_button = Qt::NoButton;
  replayed.m_modifiers = event.m_modifiers;
  m_tool->move(m_context, replayed);
  m_signal();
  return m_tool->is_busy();
}

void ToolManager::take_request() {
  if(auto name = m_context.take_tool_request(); name && has_tool(*name)) {
    set_tool(*name);
  }
}
