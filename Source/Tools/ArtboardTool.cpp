#include "Rellu/Commands/CreationCommands.hpp"
#include "Rellu/Tools/ArtboardTool.hpp"

using namespace Rellu;

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
  return std::make_unique<InsertNodeCommand>(
    "Artboard", NodeId(), std::move(header), std::move(artboard));
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
