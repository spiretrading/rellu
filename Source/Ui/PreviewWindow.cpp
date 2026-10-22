#include <algorithm>
#include <cmath>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>
#include "Rellu/Prototype/KeyNames.hpp"
#include "Rellu/Prototype/StageDocument.hpp"
#include "Rellu/Rendering/StripRasterizer.hpp"
#include "Rellu/Text/FontLibrary.hpp"
#include "Rellu/Text/TextEditing.hpp"
#include "Rellu/Text/TextLayout.hpp"
#include "Rellu/Ui/PreviewWindow.hpp"
#include "Rellu/Ui/TextMeasurement.hpp"
#include "Rellu/Ui/Translation.hpp"

using namespace Rellu;

namespace {
  const auto BACKGROUND = QColor(0, 0, 0);
  constexpr auto SCREEN_FRACTION = 0.9;
  constexpr auto WHEEL_STEP = 120.0;
  constexpr auto LINE_SCROLL = 48.0;

  bool is_artboard(const Document& document, const NodeId& id) {
    auto node = document.find(id);
    return node && node->get_artboard() && document.is_on_canvas(id);
  }
}

class PreviewWindow::View : public Canvas {
  public:
    explicit View(PreviewWindow& window)
        : m_window(&window) {
      setFocusPolicy(Qt::StrongFocus);
      setMouseTracking(true);
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
      if(event->button() != Qt::LeftButton || !m_window->m_player) {
        Canvas::mousePressEvent(event);
        return;
      }
      m_window->handle_changed(m_window->m_player->press(get_point(*event)));
      event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override {
      if(!m_window->m_player) {
        Canvas::mouseMoveEvent(event);
        return;
      }
      m_window->handle_changed(m_window->m_player->move(get_point(*event)));
      event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
      if(event->button() != Qt::LeftButton || !m_window->m_player) {
        Canvas::mouseReleaseEvent(event);
        return;
      }
      m_window->handle_changed(
        m_window->m_player->release(get_point(*event)));
      event->accept();
    }

    void leaveEvent(QEvent* event) override {
      if(m_window->m_player) {
        m_window->handle_changed(m_window->m_player->leave());
      }
      Canvas::leaveEvent(event);
    }

    void wheelEvent(QWheelEvent* event) override {
      if(!m_window->m_player) {
        Canvas::wheelEvent(event);
        return;
      }
      auto delta = Point(event->pixelDelta().x(), event->pixelDelta().y());
      if(delta == Point(0, 0)) {
        delta = Point(event->angleDelta().x() / WHEEL_STEP * LINE_SCROLL,
          event->angleDelta().y() / WHEEL_STEP * LINE_SCROLL);
      }
      auto position = m_window->to_viewport(
        Point(event->position().x(), event->position().y()));
      m_window->handle_changed(m_window->m_player->scroll(position,
        Point(-delta.m_x / m_window->m_zoom, -delta.m_y / m_window->m_zoom)));
      event->accept();
    }

    void keyPressEvent(QKeyEvent* event) override {
      if(m_window->m_player && !event->isAutoRepeat()) {
        auto chord = make_key_chord(event->key(), event->modifiers(),
          event->text().toStdU16String());
        if(m_window->m_player->press_key(chord)) {
          m_window->handle_changed(true);
          event->accept();
          return;
        }
      }
      Canvas::keyPressEvent(event);
    }

  private:
    PreviewWindow* m_window;

    Point get_point(const QMouseEvent& event) const {
      return m_window->to_viewport(
        Point(event.position().x(), event.position().y()));
    }
};

NodeId Rellu::find_preview_start(
    const Document& document, const std::vector<NodeId>& selection) {
  for(auto& id : selection) {
    auto current = id;
    for(auto depth = 0; depth != 512 && !current.is_nil(); ++depth) {
      auto node = document.find(current);
      if(!node) {
        break;
      }
      if(is_artboard(document, current)) {
        return current;
      }
      current = node->m_parent;
    }
  }
  auto find_top_left = [&] (const std::vector<NodeId>& candidates) {
    auto start = NodeId();
    auto corner = Point(0, 0);
    for(auto& id : candidates) {
      auto node = document.find(id);
      if(!node || !node->get_artboard()) {
        continue;
      }
      auto& frame = node->get_artboard()->m_frame;
      if(start.is_nil() || frame.m_y < corner.m_y ||
          (frame.m_y == corner.m_y && frame.m_x < corner.m_x)) {
        start = id;
        corner = frame.get_top_left();
      }
    }
    return start;
  };
  auto roots = std::vector<NodeId>();
  for(auto& flow : document.get_prototype().m_flows) {
    roots.push_back(flow.m_artboard);
  }
  if(auto root = find_top_left(roots); !root.is_nil()) {
    return root;
  }
  return find_top_left(document.get_canvas());
}

PreviewWindow::PreviewWindow(QWidget* parent)
    : QWidget(parent, Qt::Window),
      m_document(nullptr),
      m_view(new View(*this)),
      m_zoom(1),
      m_offset(0, 0) {
  setObjectName("preview_window");
  setWindowTitle(translate("preview", "Preview"));
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_view);
  m_view->get_renderer().set_background(BACKGROUND);
  m_frames.setInterval(FRAME_INTERVAL);
  m_frames.setTimerType(Qt::PreciseTimer);
  QObject::connect(&m_frames, &QTimer::timeout, this, [this] {
    auto elapsed = m_clock.isValid() ? m_clock.restart() / 1000.0 : 0.0;
    advance(elapsed);
  });
  m_changes.setSingleShot(true);
  m_changes.setInterval(CHANGE_DELAY);
  QObject::connect(&m_changes, &QTimer::timeout, this, [this] {
    reload();
  });
}

PreviewWindow::~PreviewWindow() {
  m_frames.stop();
  m_changes.stop();
  m_subscription.release();
  m_view->get_renderer().set_draw_list(nullptr);
  m_composer.reset();
  m_player.reset();
}

void PreviewWindow::play(Document& document, const NodeId& artboard) {
  stop();
  m_document = &document;
  m_subscription = document.subscribe([this] (const auto& changes) {
    m_changes.start();
  });
  auto position = PreviewPosition();
  position.m_artboard = artboard;
  rebuild(position);
  resize_to_artboard();
}

void PreviewWindow::stop() {
  m_frames.stop();
  m_changes.stop();
  m_subscription.release();
  m_view->get_renderer().set_draw_list(nullptr);
  m_composer.reset();
  m_player.reset();
  m_document = nullptr;
  m_shown = NodeId();
  m_view->update();
}

Document* PreviewWindow::get_document() const {
  return m_document;
}

PreviewPlayer* PreviewWindow::get_player() const {
  return m_player.get();
}

Canvas& PreviewWindow::get_view() const {
  return *m_view;
}

double PreviewWindow::get_zoom() const {
  return m_zoom;
}

Point PreviewWindow::to_viewport(Point point) const {
  return Point((point.m_x - m_offset.m_x) / m_zoom,
    (point.m_y - m_offset.m_y) / m_zoom);
}

void PreviewWindow::reload() {
  m_changes.stop();
  if(!m_document || !m_player) {
    return;
  }
  rebuild(m_player->get_position());
}

void PreviewWindow::advance(double seconds) {
  if(!m_player) {
    return;
  }
  auto is_animating = m_player->is_animating();
  handle_changed(m_player->advance(seconds) || is_animating);
}

void PreviewWindow::update_frame() {
  if(!m_player || !m_composer) {
    m_view->get_renderer().set_draw_list(nullptr);
    m_view->update();
    return;
  }
  fit();
  m_view->get_renderer().set_draw_list(
    m_composer->compose(m_player->get_layers()));
  m_view->get_renderer().set_view(make_translation(m_offset.m_x,
    m_offset.m_y) * make_scale(m_zoom, m_zoom));
  m_view->update();
}

void PreviewWindow::set_close_handler(std::function<void ()> handler) {
  m_close_handler = std::move(handler);
}

void PreviewWindow::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  update_frame();
}

void PreviewWindow::closeEvent(QCloseEvent* event) {
  stop();
  QWidget::closeEvent(event);
  if(m_close_handler) {
    m_close_handler();
  }
}

void PreviewWindow::rebuild(const PreviewPosition& position) {
  m_view->get_renderer().set_draw_list(nullptr);
  m_composer.reset();
  m_player.reset();
  m_player = std::make_unique<PreviewPlayer>(copy_document(*m_document),
    make_text_measurer(get_default_font_library()));
  m_player->set_link_handler([] (const std::string& url) {
    auto address = QString::fromStdString(url).trimmed();
    if(!address.contains("://") && !address.startsWith("mailto:")) {
      address.prepend("https://");
    }
    QDesktopServices::openUrl(QUrl(address));
  });
  m_player->set_text_link_finder([] (const Node& node, Point position) {
    auto text = node.get_text();
    if(!text || text->m_content->empty()) {
      return std::string();
    }
    auto lines = measure_lines(*text, get_default_font_library());
    auto index = std::min(hit_test(lines, *text->m_content, position),
      text->m_content->size() - 1);
    auto start = std::size_t(0);
    for(auto& run : text->m_runs) {
      if(index < start + run.m_length) {
        return run.m_hyperlink;
      }
      start += run.m_length;
    }
    return std::string();
  });
  m_composer = std::make_unique<PreviewComposer>(
    m_player->get_stage(), StripRasterizer::TOLERANCE);
  m_player->start(position);
  m_clock.restart();
  handle_changed(true);
}

void PreviewWindow::fit() {
  auto viewport = m_player ? m_player->get_viewport() : Size(0, 0);
  if(viewport.m_width <= 0 || viewport.m_height <= 0 || width() <= 0 ||
      height() <= 0) {
    m_zoom = 1;
    m_offset = Point(0, 0);
    return;
  }
  auto zoom = std::min(width() / viewport.m_width,
    height() / viewport.m_height);
  if(zoom != m_zoom && m_composer) {
    m_composer->set_tolerance(StripRasterizer::TOLERANCE /
      std::max(zoom * devicePixelRatio(), 1.0 / 64));
  }
  m_zoom = zoom;
  m_player->set_window_height(height() / zoom);
  m_offset = Point((width() - viewport.m_width * zoom) / 2,
    (height() - viewport.m_height * zoom) / 2);
}

void PreviewWindow::resize_to_artboard() {
  if(!m_player) {
    return;
  }
  auto viewport = m_player->get_viewport();
  if(viewport.m_width <= 0 || viewport.m_height <= 0) {
    return;
  }
  auto size = QSize(static_cast<int>(std::ceil(viewport.m_width)),
    static_cast<int>(std::ceil(viewport.m_height)));
  if(auto screen = this->screen() ? this->screen() :
      QGuiApplication::primaryScreen()) {
    auto available = screen->availableGeometry().size() * SCREEN_FRACTION;
    if(size.width() > available.width() ||
        size.height() > available.height()) {
      auto scale = std::min(
        static_cast<double>(available.width()) / size.width(),
        static_cast<double>(available.height()) / size.height());
      size = QSize(static_cast<int>(size.width() * scale),
        static_cast<int>(size.height() * scale));
    }
  }
  resize(size);
}

void PreviewWindow::update_timer() {
  auto is_needed = m_player &&
    (m_player->is_animating() || m_player->get_next_time());
  if(is_needed && !m_frames.isActive()) {
    m_clock.restart();
    m_frames.start();
  } else if(!is_needed && m_frames.isActive()) {
    m_frames.stop();
  }
}

void PreviewWindow::handle_changed(bool is_changed) {
  if(!m_player) {
    return;
  }
  auto& artboard = m_player->get_artboard();
  if(artboard != m_shown) {
    m_shown = artboard;
    is_changed = true;
  }
  if(is_changed) {
    update_frame();
  }
  update_timer();
}
