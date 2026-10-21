#include <memory>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Tools/ArtboardCreation.hpp"
#include "Rellu/Tools/ViewFit.hpp"

using namespace Rellu;

NodeId Rellu::create_clicked_artboard(ToolContext& context, Point point) {
  auto& editor = context.get_editor();
  auto size = context.get_size();
  auto view = Rect(point.m_x, point.m_y, 0, 0);
  if(auto inverse = invert(context.get_view())) {
    view = inverse->map(Rect(0, 0, size.m_width, size.m_height));
  }
  auto& edit = editor.get_context();
  while(!edit.get_container().is_nil()) {
    edit.exit();
  }
  auto preset = get_last_used_preset();
  auto created = dynamic_cast<const CreateArtboardCommand*>(editor.execute(
    std::make_unique<CreateArtboardCommand>(preset, point, view)));
  if(!created) {
    return NodeId();
  }
  set_last_used_preset(preset);
  auto artboard = created->get_artboard();
  auto& frame = context.get_document().get(artboard).get_artboard()->m_frame;
  if(view.contains(frame)) {
    return artboard;
  }
  if(frame.m_width > view.m_width || frame.m_height > view.m_height) {
    context.zoom_to(frame);
  } else {
    context.zoom_to(
      get_fitted_region(frame.get_center(), context.get_zoom(), size));
  }
  return artboard;
}
