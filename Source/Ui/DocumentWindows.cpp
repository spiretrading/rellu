#include <algorithm>
#include <cmath>
#include <utility>
#include <QCoreApplication>
#include <QEvent>
#include <QFileInfo>
#include <QSize>
#include "Rellu/Ui/DocumentSession.hpp"
#include "Rellu/Ui/DocumentWindows.hpp"

using namespace Rellu;

namespace {
  constexpr auto CASCADE_WIDTH = 1657.0 / 1.5;
  constexpr auto CASCADE_HEIGHT = 1153.0 / 1.5;
  constexpr auto CASCADE_STEP_X = 59.0 / 1.5;
  constexpr auto CASCADE_STEP_Y = 48.0 / 1.5;

  class ActivationFilter : public QObject {
    public:
      explicit ActivationFilter(std::function<void (QWidget&)> handler)
        : m_handler(std::move(handler)) {}

    protected:
      bool eventFilter(QObject* watched, QEvent* event) override {
        if(event->type() == QEvent::WindowActivate) {
          if(auto widget = qobject_cast<QWidget*>(watched)) {
            m_handler(*widget);
          }
        }
        return QObject::eventFilter(watched, event);
      }

    private:
      std::function<void (QWidget&)> m_handler;
  };
}

DocumentWindows::DocumentWindows()
  : m_empty_handler([] {
      QCoreApplication::quit();
    }),
    m_recent_files(MAXIMUM_RECENT_FILES),
    m_filter(std::make_unique<ActivationFilter>([this] (auto& widget) {
      on_activated(widget);
    })) {}

DocumentWindows::~DocumentWindows() {
  auto entries = std::move(m_entries);
  m_entries.clear();
  m_activations.clear();
  for(auto& entry : entries) {
    QObject::disconnect(entry.m_connection);
    entry.m_widget->removeEventFilter(m_filter.get());
  }
}

void DocumentWindows::set_factory(Factory factory) {
  m_factory = std::move(factory);
}

void DocumentWindows::set_empty_handler(std::function<void ()> handler) {
  m_empty_handler = std::move(handler);
}

void DocumentWindows::set_file_tracking(
    FileIdentifier identifier, FileLocator locator) {
  m_identifier = std::move(identifier);
  m_locator = std::move(locator);
}

std::vector<DocumentWindow*> DocumentWindows::get_windows() const {
  auto windows = std::vector<DocumentWindow*>();
  for(auto& entry : m_entries) {
    windows.push_back(entry.m_window.get());
  }
  return windows;
}

std::vector<DocumentWindow*> DocumentWindows::get_menu_windows(
    const DocumentWindow& window) const {
  auto windows = std::vector<DocumentWindow*>();
  auto current = static_cast<DocumentWindow*>(nullptr);
  for(auto activation : m_activations) {
    if(activation == &window) {
      current = activation;
    } else {
      windows.push_back(activation);
    }
  }
  if(current) {
    windows.push_back(current);
  }
  return windows;
}

DocumentWindow* DocumentWindows::get_active() const {
  if(m_activations.empty()) {
    return nullptr;
  }
  return m_activations.front();
}

DocumentWindow* DocumentWindows::find(const QString& path) const {
  if(path.isEmpty()) {
    return nullptr;
  }
  for(auto& entry : m_entries) {
    auto window_path = entry.m_window->get_path();
    if(!window_path.isEmpty() && is_same_file(window_path, path)) {
      return entry.m_window.get();
    }
  }
  return nullptr;
}

bool DocumentWindows::open(const QString& path, const QWidget* host) {
  if(auto window = find(path)) {
    activate(*window);
    add_recent_file(path);
    return true;
  }
  auto window = m_factory ? m_factory(path, nullptr) : nullptr;
  if(!window) {
    return false;
  }
  add(std::move(window), host);
  add_recent_file(path);
  return true;
}

bool DocumentWindows::make_new() {
  return make_new(make_new_document());
}

bool DocumentWindows::make_new(
    std::unique_ptr<Document> document, const QWidget* host) {
  auto window =
    m_factory ? m_factory(QString(), std::move(document)) : nullptr;
  if(!window) {
    return false;
  }
  add(std::move(window), host);
  return true;
}

DocumentWindow& DocumentWindows::add(
    std::unique_ptr<DocumentWindow> window, const QWidget* host) {
  auto& added = *window;
  auto& widget = added.get_widget();
  if(host) {
    added.place(*host);
  } else if(auto active = get_active()) {
    added.place(*active);
  }
  widget.installEventFilter(m_filter.get());
  auto connection = QObject::connect(&widget, &QObject::destroyed,
    [this] (QObject* object) {
      on_destroyed(object);
    });
  m_entries.push_back(Entry(std::move(window), &widget, connection));
  m_activations.insert(m_activations.begin(), &added);
  activate(added);
  m_signal();
  return added;
}

void DocumentWindows::remove(DocumentWindow& window) {
  auto entry = std::find_if(m_entries.begin(), m_entries.end(),
    [&] (const auto& entry) {
      return entry.m_window.get() == &window;
    });
  if(entry == m_entries.end()) {
    return;
  }
  QObject::disconnect(entry->m_connection);
  auto widget = entry->m_widget;
  widget->removeEventFilter(m_filter.get());
  entry->m_window.release();
  m_entries.erase(entry);
  std::erase(m_activations, &window);
  widget->deleteLater();
  m_signal();
  if(m_entries.empty() && m_empty_handler) {
    m_empty_handler();
  }
}

void DocumentWindows::activate(DocumentWindow& window) {
  auto& widget = window.get_widget();
  if(widget.isMinimized()) {
    widget.setWindowState(widget.windowState() & ~Qt::WindowMinimized);
  }
  widget.show();
  widget.raise();
  widget.activateWindow();
  promote(window);
}

bool DocumentWindows::close_all() {
  for(auto window : get_windows()) {
    auto is_kept = std::any_of(m_entries.begin(), m_entries.end(),
      [&] (const auto& entry) {
        return entry.m_window.get() == window;
      });
    if(!is_kept) {
      continue;
    }
    activate(*window);
    if(!window->get_widget().close()) {
      return false;
    }
  }
  return m_entries.empty();
}

const QStringList& DocumentWindows::get_recent_files() const {
  return m_recent_files.get_paths();
}

QList<QByteArray> DocumentWindows::get_recent_identities() const {
  auto identities = QList<QByteArray>();
  for(auto& path : m_recent_files.get_paths()) {
    identities.push_back(m_identities.value(path));
  }
  return identities;
}

void DocumentWindows::set_recent_files(const QStringList& paths) {
  set_recent_files(paths, QList<QByteArray>());
}

void DocumentWindows::set_recent_files(
    const QStringList& paths, const QList<QByteArray>& identities) {
  m_recent_files.clear();
  m_identities.clear();
  for(auto i = paths.size() - 1; i >= 0; --i) {
    if(paths[i].isEmpty()) {
      continue;
    }
    m_recent_files.add(paths[i]);
    record(m_recent_files.get_paths().front(),
      i < identities.size() ? identities[i] : QByteArray());
  }
  m_signal();
}

void DocumentWindows::locate_recent_files() {
  auto paths = m_recent_files.get_paths();
  auto identities = get_recent_identities();
  auto is_changed = false;
  for(auto i = 0; i != paths.size(); ++i) {
    if(QFileInfo::exists(paths[i])) {
      if(identities[i].isEmpty() && m_identifier) {
        identities[i] = m_identifier(paths[i]);
        is_changed = is_changed || !identities[i].isEmpty();
      }
    } else if(!identities[i].isEmpty() && m_locator) {
      if(auto location = m_locator(identities[i]);
          !location.isEmpty() && !is_same_file(location, paths[i])) {
        paths[i] = location;
        is_changed = true;
      }
    }
  }
  if(is_changed) {
    set_recent_files(paths, identities);
  }
}

void DocumentWindows::add_recent_file(const QString& path) {
  m_recent_files.add(path);
  auto& added = m_recent_files.get_paths().front();
  auto identity = m_identifier ? m_identifier(added) : QByteArray();
  if(identity.isEmpty()) {
    identity = m_identities.value(added);
  }
  record(added, identity);
  m_signal();
}

void DocumentWindows::remove_recent_file(const QString& path) {
  m_recent_files.remove(path);
  record(QString(), QByteArray());
  m_signal();
}

void DocumentWindows::clear_recent_files() {
  m_recent_files.clear();
  m_identities.clear();
  m_signal();
}

void DocumentWindows::record(const QString& path, const QByteArray& identity) {
  if(!path.isEmpty()) {
    m_identities.insert(path, identity);
  }
  auto& paths = m_recent_files.get_paths();
  m_identities.removeIf([&] (QHash<QString, QByteArray>::iterator entry) {
    return !paths.contains(entry.key());
  });
}

void DocumentWindows::update() {
  m_signal();
}

Subscription DocumentWindows::subscribe(std::function<void ()> observer) {
  return m_signal.subscribe(std::move(observer));
}

void DocumentWindows::promote(DocumentWindow& window) {
  auto activation =
    std::find(m_activations.begin(), m_activations.end(), &window);
  if(activation == m_activations.end() ||
      activation == m_activations.begin()) {
    return;
  }
  std::rotate(m_activations.begin(), activation, activation + 1);
  m_signal();
}

void DocumentWindows::on_activated(QWidget& widget) {
  auto entry = std::find_if(m_entries.begin(), m_entries.end(),
    [&] (const auto& entry) {
      return entry.m_widget == &widget;
    });
  if(entry != m_entries.end()) {
    promote(*entry->m_window);
  }
}

void DocumentWindows::on_destroyed(QObject* object) {
  auto entry = std::find_if(m_entries.begin(), m_entries.end(),
    [&] (const auto& entry) {
      return entry.m_widget == object;
    });
  if(entry == m_entries.end()) {
    return;
  }
  auto window = entry->m_window.release();
  m_entries.erase(entry);
  std::erase(m_activations, window);
  m_signal();
}

QPoint Rellu::get_cascade_position(const QPoint& position, double ratio) {
  return position + QPoint(
    static_cast<int>(std::lround(CASCADE_STEP_X * ratio)),
    static_cast<int>(std::lround(CASCADE_STEP_Y * ratio)));
}

QRect Rellu::get_cascade_bounds(const QRect& bounds, double ratio) {
  return QRect(get_cascade_position(bounds.topLeft(), ratio),
    QSize(static_cast<int>(std::lround(CASCADE_WIDTH * ratio)),
      static_cast<int>(std::lround(CASCADE_HEIGHT * ratio))));
}

QString Rellu::make_window_title(const QString& name) {
  return name + ' ' + QChar(0x200E) + "- Rellu";
}
