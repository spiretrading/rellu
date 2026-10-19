#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "Rellu/Commands/Bounds.hpp"
#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Commands/StructureCommands.hpp"
#include "Rellu/Tools/ArtboardTool.hpp"

using namespace Rellu;

namespace {
  bool is_enclosed(const Rect& frame, const Rect& bounds) {
    return bounds.get_left() >= frame.get_left() &&
      bounds.get_right() <= frame.get_right() &&
      bounds.get_top() >= frame.get_top() &&
      bounds.get_bottom() <= frame.get_bottom();
  }

  class DrawArtboardCommand : public Command {
    public:
      DrawArtboardCommand(
          NodeHeader header, Artboard artboard, bool is_capturing)
        : m_insert("Artboard", NodeId(), std::move(header),
            std::move(artboard)),
          m_is_capturing(is_capturing) {}

      std::string get_label() const override {
        return m_insert.get_label();
      }

      void apply(const Document& document, Transaction& transaction) override {
        m_insert.apply(document, transaction);
        if(!m_is_capturing) {
          return;
        }
        auto& id = m_insert.get_node();
        auto node = document.find(id);
        if(!node || !node->get_artboard()) {
          return;
        }
        auto frame = node->get_artboard()->m_frame;
        auto captured = std::vector<NodeId>();
        for(auto& child : document.get_canvas()) {
          auto loose = document.find(child);
          if(child == id || !loose || loose->get_artboard()) {
            continue;
          }
          if(auto bounds = get_world_bounds(document, child);
              bounds && is_enclosed(frame, *bounds)) {
            captured.push_back(child);
          }
        }
        if(captured.empty()) {
          return;
        }
        auto reparent = ReparentCommand(std::move(captured), id, 0);
        reparent.apply(document, transaction);
      }

      bool merge(const Command& command) override {
        auto draw = dynamic_cast<const DrawArtboardCommand*>(&command);
        if(!draw || !m_insert.merge(draw->m_insert)) {
          return false;
        }
        m_is_capturing = draw->m_is_capturing;
        return true;
      }

      std::optional<std::vector<NodeId>> get_selection() const override {
        return m_insert.get_selection();
      }

    private:
      InsertNodeCommand m_insert;
      bool m_is_capturing;
  };
}

ArtboardTool::ArtboardTool()
  : DrawTool("Artboard \xE2\x80\x93 ") {}

std::unique_ptr<Command> ArtboardTool::make_command(ToolContext& context,
    NodeHeader header, Point start, Point end,
    Qt::KeyboardModifiers modifiers) {
  auto box = make_drawn_box(start, end, modifiers);
  if(!(box.m_width > 0) || !(box.m_height > 0)) {
    return nullptr;
  }
  auto artboard = Artboard();
  artboard.m_frame = box;
  artboard.m_background = SolidFill(make_color(255, 255, 255));
  return std::make_unique<DrawArtboardCommand>(
    std::move(header), std::move(artboard), false);
}

void ArtboardTool::complete(ToolContext& context, const NodeId& id) {
  auto node = context.get_document().find(id);
  if(!node || !node->get_artboard()) {
    return;
  }
  context.get_editor().merge(std::make_unique<DrawArtboardCommand>(
    node->m_header, *node->get_artboard(), true));
}

NodeId ArtboardTool::find_parent(
    const ToolContext& context, Point point) const {
  return NodeId();
}

void ArtboardTool::click(ToolContext& context, Point point) {
  auto& editor = context.get_editor();
  auto artboard = context.get_titles().find(context.to_view(point));
  if(artboard.is_nil()) {
    artboard = find_drawing_parent(context.get_document(), point);
  }
  if(artboard.is_nil()) {
    editor.get_selection().clear();
    return;
  }
  auto& edit = editor.get_context();
  while(!edit.get_container().is_nil()) {
    edit.exit();
  }
  editor.get_selection().select(artboard);
}
