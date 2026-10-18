#include <cmath>
#include "Rellu/Tools/ToolContext.hpp"

using namespace Rellu;

ToolContext::ToolContext(
    Editor& editor, HitTester& hit_tester, ArtboardTitles& titles)
    : m_editor(&editor),
      m_hit_tester(&hit_tester),
      m_titles(&titles),
      m_snap_service(&m_default_snap_service),
      m_measurer(estimate_text_bounds),
      m_scale(1),
      m_size(0, 0),
      m_revision(0) {
  m_subscription = editor.get_document().subscribe([this] (const auto&) {
    ++m_revision;
  });
}

Editor& ToolContext::get_editor() const {
  return *m_editor;
}

Document& ToolContext::get_document() const {
  return m_editor->get_document();
}

HitTester& ToolContext::get_hit_tester() const {
  return *m_hit_tester;
}

ArtboardTitles& ToolContext::get_titles() const {
  return *m_titles;
}

SnapService& ToolContext::get_snap_service() const {
  return *m_snap_service;
}

void ToolContext::set_snap_service(SnapService& service) {
  m_snap_service = &service;
}

const TextMeasurer& ToolContext::get_text_measurer() const {
  return m_measurer;
}

void ToolContext::set_text_measurer(TextMeasurer measurer) {
  m_measurer = std::move(measurer);
}

const Matrix& ToolContext::get_view() const {
  return m_view;
}

double ToolContext::get_scale() const {
  return m_scale;
}

Size ToolContext::get_size() const {
  return m_size;
}

void ToolContext::set_view(const Matrix& view, double scale, Size size) {
  if(auto inverse = invert(view)) {
    m_view = view;
    m_inverse = *inverse;
  }
  m_scale = scale;
  m_size = size;
  update_titles();
}

double ToolContext::get_zoom() const {
  return std::sqrt(std::abs(m_view.get_determinant()));
}

Point ToolContext::to_document(Point point) const {
  return m_inverse.map(point);
}

Point ToolContext::to_view(Point point) const {
  return m_view.map(point);
}

void ToolContext::update_titles() {
  m_titles->lay_out(get_document(), m_view, m_size);
}

std::uint64_t ToolContext::get_revision() const {
  return m_revision;
}
