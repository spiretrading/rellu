#include <algorithm>
#include <memory>
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Tools/ArtboardCreation.hpp"

using namespace Rellu;

namespace {
  constexpr auto REVEAL_MARGIN = 24.0;
}

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
  auto zoom = context.get_zoom();
  auto fit = Size(std::max(size.m_width - 2 * REVEAL_MARGIN, 1.0) / zoom,
    std::max(size.m_height - 2 * REVEAL_MARGIN, 1.0) / zoom);
  if(frame.m_width > fit.m_width || frame.m_height > fit.m_height) {
    context.zoom_to(frame);
  } else {
    auto center = frame.get_center();
    context.zoom_to(Rect(center.m_x - fit.m_width / 2,
      center.m_y - fit.m_height / 2, fit.m_width, fit.m_height));
  }
  return artboard;
}
