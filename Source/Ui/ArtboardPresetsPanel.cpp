#include <memory>
#include <utility>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QVBoxLayout>
#include "Rellu/Ui/ArtboardPresetsPanel.hpp"
#include "Rellu/Ui/Translation.hpp"
#include "Colors.hpp"

using namespace Rellu;

namespace {
  constexpr auto ROW_HEIGHT = 48;
  constexpr auto MARGIN = 16;
  constexpr auto NAME_MARGIN = 35;
  const auto GLYPH_COLOR = QColor(0xD8, 0xD8, 0xD8);

  QString format_size(const Size& size) {
    return QString("%1 x %2").arg(size.m_width).arg(size.m_height);
  }

  QSizeF get_glyph_size(ArtboardDevice device) {
    if(device == ArtboardDevice::TABLET) {
      return QSizeF(11, 15);
    } else if(device == ArtboardDevice::DESKTOP) {
      return QSizeF(16, 11);
    } else if(device == ArtboardDevice::SOCIAL) {
      return QSizeF(13, 13);
    } else if(device == ArtboardDevice::WATCH) {
      return QSizeF(8, 11);
    }
    return QSizeF(6, 15);
  }

  class PresetRow : public QWidget {
    public:
      PresetRow(const ArtboardPreset& preset, ArtboardDevice device,
          std::function<void ()> handler)
          : m_device(device),
            m_handler(std::move(handler)),
            m_is_hovered(false) {
        setObjectName("artboard_preset");
        auto label =
          translate("presets", QString::fromStdString(preset.m_name));
        setAccessibleName(label);
        setFixedHeight(ROW_HEIGHT);
        auto layout = new QHBoxLayout(this);
        layout->setContentsMargins(NAME_MARGIN, 0, MARGIN, 0);
        layout->setSpacing(8);
        auto name = new QLabel(label);
        name->setObjectName("row_label");
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        layout->addWidget(name, 1);
        auto size = new QLabel(format_size(preset.m_size));
        size->setObjectName("field_label");
        layout->addWidget(size);
      }

    protected:
      void mouseReleaseEvent(QMouseEvent* event) override {
        if(event->button() == Qt::LeftButton &&
            rect().contains(event->position().toPoint())) {
          m_handler();
        }
      }

      void enterEvent(QEnterEvent* event) override {
        m_is_hovered = true;
        update();
      }

      void leaveEvent(QEvent* event) override {
        m_is_hovered = false;
        update();
      }

      void paintEvent(QPaintEvent* event) override {
        auto painter = QPainter(this);
        if(m_is_hovered) {
          painter.fillRect(rect(), Colors::HOVER);
        }
        auto glyph = get_glyph_size(m_device);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(GLYPH_COLOR);
        painter.drawRoundedRect(QRectF(QPointF(MARGIN,
          (height() - glyph.height()) / 2), glyph), 1, 1);
      }

    private:
      ArtboardDevice m_device;
      std::function<void ()> m_handler;
      bool m_is_hovered;
  };
}

ArtboardPresetsPanel::ArtboardPresetsPanel(QWidget* parent)
    : QWidget(parent),
      m_editor(nullptr) {
  setObjectName("artboard_presets");
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  for(auto& group : get_artboard_presets()) {
    auto heading = new QLabel(
      translate("presets", QString::fromStdString(group.m_name)));
    heading->setObjectName("section_heading");
    heading->setContentsMargins(MARGIN, 0, MARGIN, 0);
    heading->setFixedHeight(ROW_HEIGHT);
    heading->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(heading);
    for(auto& preset : group.m_presets) {
      layout->addWidget(new PresetRow(preset, group.m_device,
        [this, preset] {
          create(preset);
        }));
    }
  }
}

Editor* ArtboardPresetsPanel::get_editor() const {
  return m_editor;
}

void ArtboardPresetsPanel::set_editor(Editor* editor) {
  m_editor = editor;
}

void ArtboardPresetsPanel::set_view_provider(ViewProvider provider) {
  m_view_provider = std::move(provider);
}

void ArtboardPresetsPanel::set_reveal_handler(RevealHandler handler) {
  m_reveal_handler = std::move(handler);
}

NodeId ArtboardPresetsPanel::create(const ArtboardPreset& preset) {
  if(!m_editor) {
    return NodeId();
  }
  auto view = Rect(0, 0, 0, 0);
  if(m_view_provider) {
    view = m_view_provider().value_or(view);
  }
  auto& context = m_editor->get_context();
  while(!context.get_container().is_nil()) {
    context.exit();
  }
  auto command = std::make_unique<CreateArtboardCommand>(
    preset, m_editor->get_selection().get_nodes(), view);
  auto created = dynamic_cast<const CreateArtboardCommand*>(
    m_editor->execute(std::move(command)));
  if(!created) {
    return NodeId();
  }
  set_last_used_preset(preset);
  auto artboard = created->get_artboard();
  if(m_reveal_handler) {
    if(auto node = m_editor->get_document().find(artboard)) {
      m_reveal_handler(node->get_artboard()->m_frame);
    }
  }
  return artboard;
}
