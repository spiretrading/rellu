#include <utility>
#include "Rellu/Commands/ArtboardPlacement.hpp"
#include "Rellu/Commands/ArtboardPresets.hpp"
#include "Rellu/Model/Transaction.hpp"

using namespace Rellu;

namespace {
  ArtboardPreset make_preset(std::string name, double width, double height) {
    return ArtboardPreset(std::move(name), Size(width, height));
  }

  ArtboardPreset& get_last_used() {
    static auto preset = make_preset("iPhone 14 Pro", 393, 852);
    return preset;
  }
}

const std::vector<ArtboardPresetGroup>& Rellu::get_artboard_presets() {
  static const auto presets = std::vector<ArtboardPresetGroup>{
    ArtboardPresetGroup("MOBILE", ArtboardDevice::PHONE, {
      make_preset("iPhone 14 Pro Max", 430, 932),
      make_preset("iPhone 14 Pro", 393, 852),
      make_preset("iPhone 14 Plus, 13 Pro Max, 12 Pro Max", 428, 926),
      make_preset("iPhone 14, 13, 12", 390, 844),
      make_preset("iPhone 13 Pro, 12 Pro", 390, 844),
      make_preset("iPhone 13 mini", 375, 812),
      make_preset("iPhone X, XS, 11 Pro", 375, 812),
      make_preset("iPhone XR, XS Max, 11", 414, 896),
      make_preset("Google Pixel 7 Pro, 6 Pro", 412, 892),
      make_preset("Google Pixel 7, 6, 6a, 5a", 412, 915),
      make_preset("Samsung Galaxy S10", 360, 760)}),
    ArtboardPresetGroup("TABLET", ArtboardDevice::TABLET, {
      make_preset("iPad Pro 12.9in", 1024, 1366),
      make_preset("iPad Pro 11in", 834, 1194),
      make_preset("iPad Air", 820, 1180),
      make_preset("iPad", 768, 1024),
      make_preset("iPad mini", 744, 1133),
      make_preset("Samsung Galaxy Tab10", 800, 1280),
      make_preset("Surface Pro 7", 1368, 912),
      make_preset("Surface Pro 8", 1440, 960)}),
    ArtboardPresetGroup("WEB/DESKTOP", ArtboardDevice::DESKTOP, {
      make_preset("Web 1920", 1920, 1080),
      make_preset("Web 1366", 1366, 768),
      make_preset("Web 1280", 1280, 800)}),
    ArtboardPresetGroup("SOCIAL MEDIA", ArtboardDevice::SOCIAL, {
      make_preset("Instagram Story", 1080, 1920),
      make_preset("Instagram Post", 1080, 1080),
      make_preset("Twitter Post", 1024, 512),
      make_preset("Twitter Header", 1500, 500),
      make_preset("Facebook Post", 1200, 630),
      make_preset("Facebook Cover", 820, 312),
      make_preset("YouTube Video", 1280, 720)}),
    ArtboardPresetGroup("WATCH", ArtboardDevice::WATCH, {
      make_preset("Watch 45mm", 198, 242),
      make_preset("Watch 44mm", 184, 224),
      make_preset("Watch 42mm", 156, 195),
      make_preset("Watch 40mm", 162, 197),
      make_preset("Watch 38mm", 136, 170)})};
  return presets;
}

const ArtboardPreset& Rellu::get_last_used_preset() {
  return get_last_used();
}

void Rellu::set_last_used_preset(const ArtboardPreset& preset) {
  get_last_used() = preset;
}

CreateArtboardCommand::CreateArtboardCommand(ArtboardPreset preset,
  std::vector<NodeId> selection, const Rect& view)
  : m_preset(std::move(preset)),
    m_selection(std::move(selection)),
    m_view(view) {}

CreateArtboardCommand::CreateArtboardCommand(
  ArtboardPreset preset, Point point, const Rect& view)
  : m_preset(std::move(preset)),
    m_point(point),
    m_view(view) {}

const NodeId& CreateArtboardCommand::get_artboard() const {
  return m_artboard;
}

std::string CreateArtboardCommand::get_label() const {
  return "Create Artboard";
}

void CreateArtboardCommand::apply(
    const Document& document, Transaction& transaction) {
  auto& size = m_preset.m_size;
  auto artboard = Artboard();
  if(m_point) {
    artboard.m_frame = place_artboard_at(document, size, *m_point, m_view);
  } else {
    artboard.m_frame = place_artboards(document, m_selection, m_view,
      Rect(0, 0, size.m_width, size.m_height));
  }
  artboard.m_background = SolidFill(make_color(255, 255, 255));
  artboard.m_viewport_height = size.m_height;
  auto header = NodeHeader();
  header.m_name = make_artboard_name(document, m_preset.m_name, false);
  m_artboard = transaction.insert(NodeId(), get_artboard_index(document),
    std::move(header), std::move(artboard));
}

std::optional<std::vector<NodeId>>
    CreateArtboardCommand::get_selection() const {
  return std::vector{m_artboard};
}
