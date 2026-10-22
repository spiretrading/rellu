#include <QApplication>
#include <QEvent>
#include <QTimer>
#include <QWidget>
#include <private/qfont_p.h>
#include <private/qfontengine_p.h>
#include "Rellu/Ui/GreyscaleText.hpp"

using namespace Rellu;

GreyscaleTextFilter::GreyscaleTextFilter(QObject* parent)
  : QObject(parent),
    m_is_refresh_pending(false) {}

bool GreyscaleTextFilter::eventFilter(QObject* watched, QEvent* event) {
  if(event->type() == QEvent::Paint || event->type() == QEvent::UpdateRequest) {
    if(watched->isWidgetType()) {
      QFontPrivate::get(static_cast<QWidget*>(watched)->font())->
        engineForScript(QChar::Script_Common);
    }
    make_greyscale_fonts();
    if(!m_is_refresh_pending) {
      m_is_refresh_pending = true;
      QTimer::singleShot(0, this, [this] {
        refresh();
      });
    }
  }
  return QObject::eventFilter(watched, event);
}

void GreyscaleTextFilter::refresh() {
  m_is_refresh_pending = false;
  if(!make_greyscale_fonts()) {
    return;
  }
  for(auto widget : QApplication::topLevelWidgets()) {
    if(widget->isVisible()) {
      widget->update();
    }
  }
}

bool Rellu::make_greyscale_fonts() {
  auto cache = QFontCache::instance();
  if(!cache) {
    return false;
  }
  auto is_changed = false;
  for(auto i = cache->engineCacheCount.begin();
      i != cache->engineCacheCount.end(); ++i) {
    auto engine = i.key();
    if(engine->type() != QFontEngine::Multi &&
        engine->glyphFormat == QFontEngine::Format_None) {
      engine->glyphFormat = QFontEngine::Format_A8;
      is_changed = true;
    }
  }
  return is_changed;
}
