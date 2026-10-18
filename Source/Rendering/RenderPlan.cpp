#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include "Rellu/Rendering/RenderPlan.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_RADIUS = 0.25;
  constexpr auto MAXIMUM_DOWNSAMPLING = 32;
  constexpr auto MAXIMUM_TAPS = 32;
  constexpr auto TEXTURE_GRANULARITY = 32;
  constexpr auto SOLID_RUN = 256u;
  constexpr auto MAXIMUM_COORDINATE = 1 << 24;
  constexpr auto NONE = std::numeric_limits<std::size_t>::max();

  using Texel = std::array<float, 4>;
  using PaintRecord = std::array<Texel, PAINT_TEXELS>;

  enum class OperationType : unsigned char {
    STRIPS,
    LAYER,
    BACKDROP
  };

  struct Operation {
    OperationType m_type;
    std::uint32_t m_first;
    std::uint32_t m_count;
    std::size_t m_index;
  };

  struct Surface {
    std::size_t m_command;
    QRect m_bounds;
    QRect m_region;
    std::vector<Operation> m_operations;
  };

  struct Target {
    int m_texture;
    QPoint m_origin;
    int m_scale;
    QSize m_size;

    QSize get_extent() const {
      return m_size * m_scale;
    }
  };

  QRect to_rect(const Strip& strip) {
    return QRect(static_cast<int>(strip.m_x), static_cast<int>(strip.m_y),
      static_cast<int>(strip.m_width), static_cast<int>(strip.m_height));
  }

  int to_coordinate(double value) {
    return static_cast<int>(std::clamp(value,
      -1.0 * MAXIMUM_COORDINATE, 1.0 * MAXIMUM_COORDINATE));
  }

  QRect round_out(const Rect& rect) {
    auto left = to_coordinate(std::floor(rect.get_left()));
    auto top = to_coordinate(std::floor(rect.get_top()));
    auto right = to_coordinate(std::ceil(rect.get_right()));
    auto bottom = to_coordinate(std::ceil(rect.get_bottom()));
    return QRect(left, top, right - left, bottom - top);
  }

  double get_scale(const Matrix& matrix) {
    return std::sqrt(std::abs(matrix.get_determinant()));
  }

  Point map_vector(const Matrix& matrix, Point vector) {
    return Point(matrix.m_a * vector.m_x + matrix.m_c * vector.m_y,
      matrix.m_b * vector.m_x + matrix.m_d * vector.m_y);
  }

  int get_margin(double radius) {
    if(radius < MINIMUM_RADIUS) {
      return 1;
    }
    return to_coordinate(std::ceil(3 * radius)) + 1;
  }

  std::array<qfloat16, 4> to_premultiplied(const Color& color) {
    auto alpha = std::clamp(color.m_alpha, 0.0, 1.0);
    return std::array<qfloat16, 4>({
      qfloat16(std::clamp(color.m_red, 0.0, 1.0) * alpha),
      qfloat16(std::clamp(color.m_green, 0.0, 1.0) * alpha),
      qfloat16(std::clamp(color.m_blue, 0.0, 1.0) * alpha),
      qfloat16(alpha)});
  }

  std::uint32_t to_texel(const Color& color) {
    auto to_byte = [] (double value) {
      return static_cast<std::uint32_t>(
        std::lround(std::clamp(value, 0.0, 1.0) * 255));
    };
    return to_byte(color.m_red) | (to_byte(color.m_green) << 8) |
      (to_byte(color.m_blue) << 16) | (to_byte(color.m_alpha) << 24);
  }

  float to_float(PaintKind kind) {
    return static_cast<float>(kind);
  }

  class Planner {
    public:
      Planner(const DrawSequence& commands, const StripScene& scene,
        const Matrix& view, QSize size, const std::optional<QRect>& region,
        const PlanOptions& options, RenderPlan& plan);

      void run();

    private:
      const DrawSequence* m_commands;
      const StripScene* m_scene;
      Matrix m_view;
      double m_scale;
      QSize m_size;
      std::optional<QRect> m_region;
      const PlanOptions* m_options;
      RenderPlan* m_plan;
      std::vector<Surface> m_surfaces;
      std::vector<int> m_command_slots;
      std::vector<int> m_slot_images;
      std::vector<int> m_free;

      const DrawCommand& get_command(std::size_t index) const;
      void add_paints();
      void add_gradient(int slot, const FillCommand& command,
        const GradientPaint& paint);
      void add_image(int slot, const FillCommand& command,
        const ImagePaint& paint,
        std::unordered_map<const QImage*, int>& images);
      void add_surfaces();
      void add_strips(Surface& surface, std::uint32_t first,
        std::uint32_t count);
      void close(std::vector<std::size_t>& stack);
      QRect get_region(const Surface& surface,
        const BeginLayerCommand& layer) const;
      QRect get_bounds(std::uint32_t first, std::uint32_t count) const;
      std::uint32_t get_first(std::size_t command) const;
      std::uint32_t get_count(std::size_t command) const;
      Target allocate(QSize size, QPoint origin, int scale);
      void release(const Target& target);
      std::size_t add_pass(const Target& target, PlanClear clear);
      PlanPass& get_pass(std::size_t index);
      void draw(std::size_t pass, PlanPipeline pipeline, std::uint32_t first,
        std::uint32_t count, PlanSource source, int backdrop = -1);
      void draw_strips(std::size_t pass, std::uint32_t first,
        std::uint32_t count);
      std::uint32_t add_span(
        const QRect& rect, const std::array<qfloat16, 4>& color, int slot);
      int add_paint();
      void set_texture_paint(int slot, const Target& target, Point offset,
        PaintKind kind, const Target* backdrop = nullptr,
        BlendMode mode = BlendMode::NORMAL);
      void emit_content(std::size_t surface, const Target& target,
        PlanClear clear);
      Target emit_layer(std::size_t surface);
      Target blur(const Target& source, const QRect& region, double radius,
        bool is_inverted, double brightness);
      void add_blur_pass(const Target& source, QSize texels,
        const Target& destination, Point step, double radius,
        bool is_inverted, double brightness);
      void add_copy(const Target& source, const QRect& region,
        const Target& destination);
  };

  Planner::Planner(const DrawSequence& commands, const StripScene& scene,
    const Matrix& view, QSize size, const std::optional<QRect>& region,
    const PlanOptions& options, RenderPlan& plan)
    : m_commands(&commands),
      m_scene(&scene),
      m_view(view),
      m_scale(get_scale(view)),
      m_size(size),
      m_region(region),
      m_options(&options),
      m_plan(&plan) {}

  void Planner::run() {
    add_paints();
    add_surfaces();
    if(m_region) {
      auto target = allocate(m_region->size(), m_region->topLeft(), 1);
      emit_content(0, target, PlanClear::BACKGROUND);
      m_plan->m_target = target.m_texture;
      return;
    }
    auto& root = m_surfaces.front();
    auto is_direct = std::all_of(root.m_operations.begin(),
      root.m_operations.end(), [] (const auto& operation) {
        return operation.m_type == OperationType::STRIPS;
      });
    auto external = Target(-1, QPoint(0, 0), 1, m_size);
    if(is_direct) {
      auto pass = add_pass(external, PlanClear::BACKGROUND);
      for(auto& operation : root.m_operations) {
        draw_strips(pass, operation.m_first, operation.m_count);
      }
    } else {
      auto target = allocate(m_size, QPoint(0, 0), 1);
      emit_content(0, target, PlanClear::BACKGROUND);
      auto slot = add_paint();
      set_texture_paint(slot, target, Point(0, 0), PaintKind::TEXTURE);
      auto span = add_span(QRect(QPoint(0, 0), m_size),
        to_premultiplied(Color(1, 1, 1, 1)), slot);
      auto pass = add_pass(external, PlanClear::CLEAR);
      draw(pass, PlanPipeline::OVER, span, 1, PlanSource(-1, target.m_texture));
      release(target);
    }
  }

  const DrawCommand& Planner::get_command(std::size_t index) const {
    return *(*m_commands)[index];
  }

  void Planner::add_paints() {
    auto& paints = m_scene->m_paints;
    m_plan->m_paints.assign(PAINT_TEXELS * paints.size(), Texel());
    m_command_slots.assign(m_commands->size(), -1);
    m_slot_images.assign(paints.size(), -1);
    auto images = std::unordered_map<const QImage*, int>();
    for(auto slot = std::size_t(0); slot != paints.size(); ++slot) {
      auto command = paints[slot];
      m_command_slots[command] = static_cast<int>(slot);
      auto fill = std::get_if<FillCommand>(&get_command(command));
      if(!fill) {
        continue;
      }
      if(auto gradient = std::get_if<GradientPaint>(&fill->m_paint)) {
        add_gradient(static_cast<int>(slot), *fill, *gradient);
      } else if(auto image = std::get_if<ImagePaint>(&fill->m_paint)) {
        add_image(static_cast<int>(slot), *fill, *image, images);
      }
    }
  }

  void Planner::add_gradient(
      int slot, const FillCommand& command, const GradientPaint& paint) {
    auto record = PaintRecord();
    auto inverse = invert(m_view * command.m_transform * paint.m_transform);
    auto constant = [&] {
      record[0] = Texel({0, 0, 1, to_float(PaintKind::LINEAR)});
    };
    if(!inverse) {
      constant();
    } else if(auto linear = std::get_if<LinearGradient>(&paint.m_geometry)) {
      auto direction = linear->m_end - linear->m_start;
      auto length = direction.m_x * direction.m_x +
        direction.m_y * direction.m_y;
      if(!(length > 0)) {
        constant();
      } else {
        auto& matrix = *inverse;
        record[0] = Texel({
          static_cast<float>((direction.m_x * matrix.m_a +
            direction.m_y * matrix.m_b) / length),
          static_cast<float>((direction.m_x * matrix.m_c +
            direction.m_y * matrix.m_d) / length),
          static_cast<float>(
            (direction.m_x * (matrix.m_tx - linear->m_start.m_x) +
              direction.m_y * (matrix.m_ty - linear->m_start.m_y)) / length),
          to_float(PaintKind::LINEAR)});
      }
    } else {
      auto& radial = std::get<RadialGradient>(paint.m_geometry);
      if(!(radial.m_radius > 0)) {
        constant();
      } else {
        auto& matrix = *inverse;
        auto focus = radial.m_focus;
        auto distance = length(focus - radial.m_center);
        auto limit = 0.999 * radial.m_radius;
        if(distance > limit) {
          focus = radial.m_center + (focus - radial.m_center) *
            (limit / distance);
        }
        record[0] = Texel({static_cast<float>(matrix.m_a),
          static_cast<float>(matrix.m_c), static_cast<float>(matrix.m_tx),
          to_float(PaintKind::RADIAL)});
        record[1] = Texel({static_cast<float>(matrix.m_b),
          static_cast<float>(matrix.m_d), static_cast<float>(matrix.m_ty), 0});
        record[2] = Texel({static_cast<float>(focus.m_x),
          static_cast<float>(focus.m_y),
          static_cast<float>(radial.m_center.m_x),
          static_cast<float>(radial.m_center.m_y)});
        record[3] = Texel({static_cast<float>(radial.m_radius), 0, 0, 0});
      }
    }
    std::copy(record.begin(), record.end(),
      m_plan->m_paints.begin() + PAINT_TEXELS * slot);
    auto row = m_plan->get_ramp_height();
    m_plan->m_ramps.resize(m_plan->m_ramps.size() + GRADIENT_RAMP_WIDTH);
    auto ramp = m_plan->m_ramps.end() - GRADIENT_RAMP_WIDTH;
    for(auto i = 0; i != GRADIENT_RAMP_WIDTH; ++i) {
      ramp[i] = to_texel(evaluate_gradient(paint.m_stops,
        static_cast<double>(i) / (GRADIENT_RAMP_WIDTH - 1)));
    }
    m_plan->m_paints[PAINT_TEXELS * slot + 1][3] = static_cast<float>(row);
  }

  void Planner::add_image(int slot, const FillCommand& command,
      const ImagePaint& paint,
      std::unordered_map<const QImage*, int>& images) {
    if(!paint.m_image || paint.m_image->isNull()) {
      return;
    }
    auto inverse = invert(m_view * command.m_transform * paint.m_transform);
    if(!inverse) {
      return;
    }
    auto entry = images.find(paint.m_image.get());
    if(entry == images.end()) {
      entry = images.emplace(paint.m_image.get(),
        static_cast<int>(m_plan->m_images.size())).first;
      m_plan->m_images.push_back(paint.m_image);
    }
    m_slot_images[slot] = entry->second;
    auto width = static_cast<double>(paint.m_image->width());
    auto height = static_cast<double>(paint.m_image->height());
    auto& matrix = *inverse;
    auto record = PaintRecord();
    record[0] = Texel({static_cast<float>(matrix.m_a / width),
      static_cast<float>(matrix.m_c / width),
      static_cast<float>(matrix.m_tx / width), to_float(PaintKind::TEXTURE)});
    record[1] = Texel({static_cast<float>(matrix.m_b / height),
      static_cast<float>(matrix.m_d / height),
      static_cast<float>(matrix.m_ty / height), 0});
    auto image_bounds = paint.m_transform.map(Rect(0, 0, width, height));
    auto margin = 1e-3 * std::max(image_bounds.m_width, image_bounds.m_height);
    auto path_bounds = Rellu::get_bounds(*command.m_path);
    if(!inflate(image_bounds, margin).contains(path_bounds)) {
      record[2] = Texel({0, 1, 0, 0});
    }
    record[3] =
      Texel({static_cast<float>(width), static_cast<float>(height), 0, 0});
    std::copy(record.begin(), record.end(),
      m_plan->m_paints.begin() + PAINT_TEXELS * slot);
  }

  void Planner::add_surfaces() {
    m_surfaces.clear();
    m_surfaces.push_back(
      Surface(NONE, QRect(), QRect(), std::vector<Operation>()));
    auto stack = std::vector<std::size_t>({0});
    for(auto i = std::size_t(0); i != m_commands->size(); ++i) {
      auto& command = get_command(i);
      if(std::holds_alternative<FillCommand>(command)) {
        add_strips(m_surfaces[stack.back()], get_first(i), get_count(i));
      } else if(std::holds_alternative<BeginLayerCommand>(command)) {
        stack.push_back(m_surfaces.size());
        m_surfaces.push_back(
          Surface(i, QRect(), QRect(), std::vector<Operation>()));
      } else if(std::holds_alternative<EndLayerCommand>(command)) {
        if(stack.size() > 1) {
          close(stack);
        }
      } else if(auto count = get_count(i)) {
        auto& surface = m_surfaces[stack.back()];
        surface.m_operations.push_back(
          Operation(OperationType::BACKDROP, get_first(i), count, i));
        if(stack.back() != 0) {
          surface.m_bounds |= get_bounds(get_first(i), count);
        }
      }
    }
    while(stack.size() > 1) {
      close(stack);
    }
  }

  void Planner::add_strips(
      Surface& surface, std::uint32_t first, std::uint32_t count) {
    if(count == 0) {
      return;
    }
    auto& operations = surface.m_operations;
    if(!operations.empty() &&
        operations.back().m_type == OperationType::STRIPS &&
        operations.back().m_first + operations.back().m_count == first) {
      operations.back().m_count += count;
    } else {
      operations.push_back(
        Operation(OperationType::STRIPS, first, count, NONE));
    }
    if(&surface != &m_surfaces.front()) {
      surface.m_bounds |= get_bounds(first, count);
    }
  }

  void Planner::close(std::vector<std::size_t>& stack) {
    auto index = stack.back();
    stack.pop_back();
    auto command = m_surfaces[index].m_command;
    auto& layer = std::get<BeginLayerCommand>(get_command(command));
    if(!(layer.m_opacity > 0)) {
      return;
    }
    auto region = get_region(m_surfaces[index], layer);
    if(region.isEmpty()) {
      return;
    }
    auto composite = region;
    if(layer.m_mask) {
      composite &= get_bounds(get_first(command), get_count(command));
      if(composite.isEmpty()) {
        return;
      }
    }
    m_surfaces[index].m_region = region;
    auto parent = stack.back();
    m_surfaces[parent].m_operations.push_back(
      Operation(OperationType::LAYER, 0, 0, index));
    if(parent != 0) {
      m_surfaces[parent].m_bounds |= composite;
    }
  }

  QRect Planner::get_region(
      const Surface& surface, const BeginLayerCommand& layer) const {
    auto& bounds = surface.m_bounds;
    if(bounds.isEmpty()) {
      return QRect();
    }
    auto region = bounds;
    for(auto& shadow : layer.m_drop_shadows) {
      auto offset = map_vector(m_view, shadow.m_offset);
      auto margin = get_margin(shadow.m_radius * m_scale);
      region |= round_out(Rect(bounds.x() + offset.m_x,
        bounds.y() + offset.m_y, bounds.width(), bounds.height())).adjusted(
          -margin, -margin, margin, margin);
    }
    for(auto& shadow : layer.m_inner_shadows) {
      auto offset = map_vector(m_view, shadow.m_offset);
      auto margin = get_margin(shadow.m_radius * m_scale) + to_coordinate(
        std::ceil(std::abs(offset.m_x) + std::abs(offset.m_y)));
      region |= bounds.adjusted(-margin, -margin, margin, margin);
    }
    if(layer.m_blur * m_scale >= MINIMUM_RADIUS) {
      auto margin = get_margin(layer.m_blur * m_scale);
      region.adjust(-margin, -margin, margin, margin);
    }
    region.adjust(-1, -1, 1, 1);
    if(layer.m_clip) {
      region &= round_out(m_view.map(*layer.m_clip));
    }
    return region & QRect(QPoint(0, 0), m_size);
  }

  QRect Planner::get_bounds(std::uint32_t first, std::uint32_t count) const {
    auto bounds = QRect();
    for(auto i = first; i != first + count; ++i) {
      bounds |= to_rect(m_scene->m_strips[i]);
    }
    return bounds;
  }

  std::uint32_t Planner::get_first(std::size_t command) const {
    return m_scene->m_offsets[command];
  }

  std::uint32_t Planner::get_count(std::size_t command) const {
    return m_scene->m_offsets[command + 1] - m_scene->m_offsets[command];
  }

  Target Planner::allocate(QSize size, QPoint origin, int scale) {
    auto round = [&] (int value) {
      if(m_region) {
        auto bucket = TEXTURE_GRANULARITY;
        while(bucket < value) {
          if(bucket + bucket / 2 >= value) {
            return bucket + bucket / 2;
          }
          bucket *= 2;
        }
        return bucket;
      }
      return std::max(1, (value + TEXTURE_GRANULARITY - 1) /
        TEXTURE_GRANULARITY) * TEXTURE_GRANULARITY;
    };
    auto bucket = QSize(round(size.width()), round(size.height()));
    auto& textures = m_plan->m_textures;
    auto match = std::find_if(m_free.begin(), m_free.end(), [&] (auto index) {
      return textures[index] == bucket;
    });
    auto texture = 0;
    if(match != m_free.end()) {
      texture = *match;
      m_free.erase(match);
    } else {
      texture = static_cast<int>(textures.size());
      textures.push_back(bucket);
    }
    return Target(texture, origin, scale, bucket);
  }

  void Planner::release(const Target& target) {
    if(target.m_texture >= 0) {
      m_free.push_back(target.m_texture);
    }
  }

  std::size_t Planner::add_pass(const Target& target, PlanClear clear) {
    auto& steps = m_plan->m_steps;
    auto uniforms = PlanUniforms();
    uniforms.fill(0);
    auto extent = target.get_extent();
    auto projection = m_options->m_correction;
    projection.ortho(static_cast<float>(target.m_origin.x()),
      static_cast<float>(target.m_origin.x() + extent.width()),
      static_cast<float>(target.m_origin.y() + extent.height()),
      static_cast<float>(target.m_origin.y()), -1, 1);
    std::copy(projection.constData(), projection.constData() + 16,
      uniforms.begin());
    if(m_region) {
      uniforms[32] = static_cast<float>(target.m_origin.x());
      uniforms[33] = static_cast<float>(target.m_origin.y());
      if(m_options->m_is_y_up) {
        uniforms[34] = static_cast<float>(extent.height());
      }
      uniforms[35] = 1;
    }
    if(clear == PlanClear::KEEP && !steps.empty()) {
      if(auto pass = std::get_if<PlanPass>(&steps.back())) {
        if(pass->m_target == target.m_texture &&
            m_plan->m_uniforms[pass->m_uniforms] == uniforms) {
          return steps.size() - 1;
        }
      }
    }
    m_plan->m_uniforms.push_back(uniforms);
    steps.push_back(PlanPass(target.m_texture, clear,
      static_cast<int>(m_plan->m_uniforms.size() - 1),
      std::vector<PlanDraw>()));
    return steps.size() - 1;
  }

  PlanPass& Planner::get_pass(std::size_t index) {
    return std::get<PlanPass>(m_plan->m_steps[index]);
  }

  void Planner::draw(std::size_t pass, PlanPipeline pipeline,
      std::uint32_t first, std::uint32_t count, PlanSource source,
      int backdrop) {
    if(count == 0) {
      return;
    }
    auto& draws = get_pass(pass).m_draws;
    if(!draws.empty()) {
      auto& last = draws.back();
      if(last.m_pipeline == pipeline && last.m_source == source &&
          last.m_backdrop == backdrop && last.m_first + last.m_count == first) {
        last.m_count += count;
        return;
      }
    }
    draws.push_back(PlanDraw(pipeline, first, count, source, backdrop));
  }

  void Planner::draw_strips(
      std::size_t pass, std::uint32_t first, std::uint32_t count) {
    auto& strips = m_scene->m_strips;
    auto last = first + count;
    auto start = first;
    auto pipeline = PlanPipeline::SOLID;
    auto image = -1;
    auto painted = first;
    for(auto i = first; i != last; ++i) {
      auto paint = strips[i].m_paint;
      if(paint < 0) {
        if(pipeline == PlanPipeline::OVER && i - painted > SOLID_RUN) {
          draw(pass, pipeline, start, painted + 1 - start,
            PlanSource(image, -1));
          start = painted + 1;
          pipeline = PlanPipeline::SOLID;
          image = -1;
        }
        continue;
      }
      auto needed = m_slot_images[static_cast<std::size_t>(paint)];
      if(pipeline == PlanPipeline::SOLID) {
        if(i - start > SOLID_RUN) {
          draw(pass, pipeline, start, i - start, PlanSource());
          start = i;
        }
        pipeline = PlanPipeline::OVER;
      } else if(needed >= 0 && image >= 0 && needed != image) {
        draw(pass, pipeline, start, i - start, PlanSource(image, -1));
        start = i;
      }
      if(needed >= 0) {
        image = needed;
      }
      painted = i;
    }
    draw(pass, pipeline, start, last - start, PlanSource(image, -1));
  }

  std::uint32_t Planner::add_span(
      const QRect& rect, const std::array<qfloat16, 4>& color, int slot) {
    m_plan->m_strips.push_back(Strip(static_cast<float>(rect.x()),
      static_cast<float>(rect.y()), static_cast<float>(rect.width()),
      static_cast<float>(rect.height()), color, -1, 0,
      static_cast<float>(slot)));
    return static_cast<std::uint32_t>(
      m_scene->m_strips.size() + m_plan->m_strips.size() - 1);
  }

  int Planner::add_paint() {
    auto slot = static_cast<int>(m_plan->m_paints.size() / PAINT_TEXELS);
    m_plan->m_paints.resize(
      m_plan->m_paints.size() + PAINT_TEXELS, Texel());
    return slot;
  }

  void Planner::set_texture_paint(int slot, const Target& target,
      Point offset, PaintKind kind, const Target* backdrop, BlendMode mode) {
    auto map = [&] (const Target& texture, Point shift) {
      auto extent = texture.get_extent();
      auto width = static_cast<double>(extent.width());
      auto height = static_cast<double>(extent.height());
      auto x = texture.m_origin.x() + shift.m_x;
      auto y = texture.m_origin.y() + shift.m_y;
      if(m_options->m_is_y_up) {
        return std::array<float, 4>({static_cast<float>(1 / width),
          static_cast<float>(-x / width), static_cast<float>(-1 / height),
          static_cast<float>(1 + y / height)});
      }
      return std::array<float, 4>({static_cast<float>(1 / width),
        static_cast<float>(-x / width), static_cast<float>(1 / height),
        static_cast<float>(-y / height)});
    };
    auto source = map(target, offset);
    auto record = PaintRecord();
    record[0] = Texel({source[0], 0, source[1], to_float(kind)});
    record[1] = Texel({0, source[2], source[3], static_cast<float>(mode)});
    if(backdrop) {
      record[2] = map(*backdrop, Point(0, 0));
    }
    record[3] = Texel({static_cast<float>(target.m_size.width()),
      static_cast<float>(target.m_size.height()), 0, 0});
    std::copy(record.begin(), record.end(),
      m_plan->m_paints.begin() + PAINT_TEXELS * slot);
  }

  void Planner::emit_content(
      std::size_t index, const Target& target, PlanClear clear) {
    auto pass = add_pass(target, clear);
    for(auto& operation : m_surfaces[index].m_operations) {
      if(operation.m_type == OperationType::STRIPS) {
        draw_strips(pass, operation.m_first, operation.m_count);
      } else if(operation.m_type == OperationType::LAYER) {
        auto& child = m_surfaces[operation.m_index];
        auto& layer =
          std::get<BeginLayerCommand>(get_command(child.m_command));
        auto source = emit_layer(operation.m_index);
        auto slot = m_command_slots[child.m_command];
        auto first = std::uint32_t(0);
        auto count = std::uint32_t(1);
        if(layer.m_mask) {
          first = get_first(child.m_command);
          count = get_count(child.m_command);
        } else {
          first = add_span(child.m_region,
            to_premultiplied(Color(1, 1, 1, layer.m_opacity)), slot);
        }
        if(layer.m_blend_mode == BlendMode::NORMAL) {
          set_texture_paint(slot, source, Point(0, 0), PaintKind::TEXTURE);
          pass = add_pass(target, PlanClear::KEEP);
          draw(pass, PlanPipeline::OVER, first, count,
            PlanSource(-1, source.m_texture));
        } else {
          auto& region = child.m_region;
          auto backdrop = allocate(region.size(), region.topLeft(), 1);
          add_copy(target, region, backdrop);
          set_texture_paint(slot, source, Point(0, 0), PaintKind::TEXTURE,
            &backdrop, layer.m_blend_mode);
          pass = add_pass(target, PlanClear::KEEP);
          draw(pass, PlanPipeline::BLEND, first, count,
            PlanSource(-1, source.m_texture), backdrop.m_texture);
          release(backdrop);
        }
        release(source);
      } else {
        auto& command =
          std::get<BackdropBlurCommand>(get_command(operation.m_index));
        auto radius = command.m_radius * m_scale;
        auto margin = get_margin(radius);
        auto region = get_bounds(operation.m_first, operation.m_count).adjusted(
          -margin, -margin, margin, margin) &
          QRect(target.m_origin, target.get_extent());
        if(index == 0) {
          region &= QRect(QPoint(0, 0), m_size);
        }
        if(region.isEmpty()) {
          continue;
        }
        auto backdrop = allocate(region.size(), region.topLeft(), 1);
        add_copy(target, region, backdrop);
        auto blurred =
          blur(backdrop, region, radius, false, command.m_brightness);
        release(backdrop);
        auto slot = m_command_slots[operation.m_index];
        set_texture_paint(slot, blurred, Point(0, 0), PaintKind::TEXTURE);
        pass = add_pass(target, PlanClear::KEEP);
        draw(pass, PlanPipeline::OVER, operation.m_first, operation.m_count,
          PlanSource(-1, blurred.m_texture));
        release(blurred);
      }
    }
  }

  Target Planner::emit_layer(std::size_t index) {
    auto region = m_surfaces[index].m_region;
    auto& layer = std::get<BeginLayerCommand>(
      get_command(m_surfaces[index].m_command));
    auto target = allocate(region.size(), region.topLeft(), 1);
    emit_content(index, target, PlanClear::CLEAR);
    for(auto& shadow : layer.m_inner_shadows) {
      auto blurred =
        blur(target, region, shadow.m_radius * m_scale, true, 0);
      auto slot = add_paint();
      set_texture_paint(slot, blurred, map_vector(m_view, shadow.m_offset),
        PaintKind::TEXTURE_ALPHA);
      auto span = add_span(region, to_premultiplied(shadow.m_color), slot);
      auto pass = add_pass(target, PlanClear::KEEP);
      draw(pass, PlanPipeline::ATOP, span, 1,
        PlanSource(-1, blurred.m_texture));
      release(blurred);
    }
    if(!layer.m_drop_shadows.empty()) {
      auto composite = allocate(region.size(), region.topLeft(), 1);
      auto clear = PlanClear::CLEAR;
      for(auto& shadow : layer.m_drop_shadows) {
        auto radius = shadow.m_radius * m_scale;
        auto source = target;
        if(radius >= MINIMUM_RADIUS) {
          source = blur(target, region, radius, false, 0);
        }
        auto slot = add_paint();
        set_texture_paint(slot, source, map_vector(m_view, shadow.m_offset),
          PaintKind::TEXTURE_ALPHA);
        auto span = add_span(region, to_premultiplied(shadow.m_color), slot);
        auto pass = add_pass(composite, clear);
        clear = PlanClear::KEEP;
        draw(pass, PlanPipeline::OVER, span, 1,
          PlanSource(-1, source.m_texture));
        if(source.m_texture != target.m_texture) {
          release(source);
        }
      }
      auto slot = add_paint();
      set_texture_paint(slot, target, Point(0, 0), PaintKind::TEXTURE);
      auto span =
        add_span(region, to_premultiplied(Color(1, 1, 1, 1)), slot);
      auto pass = add_pass(composite, PlanClear::KEEP);
      draw(pass, PlanPipeline::OVER, span, 1,
        PlanSource(-1, target.m_texture));
      release(target);
      target = composite;
    }
    if(layer.m_blur * m_scale >= MINIMUM_RADIUS) {
      auto blurred = blur(target, region, layer.m_blur * m_scale, false, 0);
      release(target);
      target = blurred;
    }
    return target;
  }

  Target Planner::blur(const Target& source, const QRect& region,
      double radius, bool is_inverted, double brightness) {
    auto factor = get_blur_downsampling(radius);
    auto current = source;
    auto level = 1;
    auto is_first = true;
    auto get_size = [&] (int scale) {
      return QSize((region.width() + scale - 1) / scale,
        (region.height() + scale - 1) / scale);
    };
    while(level < factor) {
      level *= 2;
      auto destination =
        allocate(get_size(level), region.topLeft(), level);
      add_blur_pass(current, get_size(level / 2), destination, Point(0, 0),
        0, is_first && is_inverted, 0);
      if(current.m_texture != source.m_texture) {
        release(current);
      }
      current = destination;
      is_first = false;
    }
    auto deviation = radius / level;
    if(level > 1) {
      deviation = std::sqrt(
        std::max(0.0, deviation * deviation - 1.0 / 12 - 1.0 / 6));
    }
    auto horizontal = allocate(get_size(level), region.topLeft(), level);
    add_blur_pass(current, get_size(level), horizontal,
      Point(1.0 / current.m_size.width(), 0), deviation,
      is_first && is_inverted, 0);
    if(current.m_texture != source.m_texture) {
      release(current);
    }
    auto vertical = allocate(get_size(level), region.topLeft(), level);
    add_blur_pass(horizontal, get_size(level), vertical,
      Point(0, 1.0 / horizontal.m_size.height()), deviation, false,
      brightness);
    release(horizontal);
    return vertical;
  }

  void Planner::add_blur_pass(const Target& source, QSize texels,
      const Target& destination, Point step, double radius, bool is_inverted,
      double brightness) {
    auto pass = add_pass(destination, PlanClear::CLEAR);
    auto& uniforms = m_plan->m_uniforms[get_pass(pass).m_uniforms];
    auto extent = source.get_extent();
    uniforms[20] = static_cast<float>(source.m_origin.x());
    uniforms[21] = static_cast<float>(source.m_origin.y());
    uniforms[22] = static_cast<float>(1.0 / extent.width());
    uniforms[23] = static_cast<float>(1.0 / extent.height());
    uniforms[24] = static_cast<float>(step.m_x);
    uniforms[25] = static_cast<float>(step.m_y);
    uniforms[26] = static_cast<float>(radius);
    uniforms[27] = static_cast<float>(std::min(
      MAXIMUM_TAPS, radius < 0.1 ? 0 : to_coordinate(std::ceil(3 * radius))));
    uniforms[28] = is_inverted ? 1.0f : 0.0f;
    uniforms[29] = static_cast<float>(std::clamp(brightness, -1.0, 1.0));
    uniforms[30] = m_options->m_is_y_up ? 1.0f : 0.0f;
    auto width = static_cast<double>(source.m_size.width());
    auto height = static_cast<double>(source.m_size.height());
    auto top = 0.5 / height;
    auto bottom = (texels.height() - 0.5) / height;
    if(m_options->m_is_y_up) {
      top = 1 - bottom;
      bottom = 1 - 0.5 / height;
    }
    uniforms[32] = static_cast<float>(0.5 / width);
    uniforms[33] = static_cast<float>(top);
    uniforms[34] = static_cast<float>((texels.width() - 0.5) / width);
    uniforms[35] = static_cast<float>(bottom);
    auto span = add_span(QRect(destination.m_origin, destination.get_extent()),
      to_premultiplied(Color(1, 1, 1, 1)), -1);
    draw(pass, PlanPipeline::BLUR, span, 1,
      PlanSource(-1, source.m_texture));
  }

  void Planner::add_copy(
      const Target& source, const QRect& region, const Target& destination) {
    auto texels = region.translated(-source.m_origin);
    if(m_options->m_is_y_up) {
      texels.moveTop(
        source.m_size.height() - texels.y() - texels.height());
    }
    m_plan->m_steps.push_back(
      PlanCopy(source.m_texture, texels, destination.m_texture));
  }

  void plan_render(const DrawSequence& commands, const StripScene& scene,
      const Matrix& view, QSize size, const std::optional<QRect>& region,
      const PlanOptions& options, RenderPlan& plan) {
    plan.m_steps.clear();
    plan.m_textures.clear();
    plan.m_strips.clear();
    plan.m_paints.clear();
    plan.m_ramps.clear();
    plan.m_images.clear();
    plan.m_uniforms.clear();
    plan.m_target = -1;
    auto planner = Planner(commands, scene, view, size, region, options, plan);
    planner.run();
  }
}

int RenderPlan::get_ramp_height() const {
  return static_cast<int>(m_ramps.size() / GRADIENT_RAMP_WIDTH);
}

void Rellu::make_render_plan(const DrawList& list, const StripScene& scene,
    const Matrix& view, QSize size, const PlanOptions& options,
    RenderPlan& plan) {
  make_render_plan(to_sequence(list), scene, view, size, options, plan);
}

void Rellu::make_render_plan(const DrawSequence& commands,
    const StripScene& scene, const Matrix& view, QSize size,
    const PlanOptions& options, RenderPlan& plan) {
  plan_render(commands, scene, view, size, std::nullopt, options, plan);
}

void Rellu::make_render_plan(const DrawSequence& commands,
    const StripScene& scene, const Matrix& view, QSize size,
    const QRect& region, const PlanOptions& options, RenderPlan& plan) {
  plan_render(commands, scene, view, size, region, options, plan);
}

Color Rellu::evaluate_gradient(
    const std::vector<GradientStop>& stops, double offset) {
  if(stops.empty()) {
    return Color(0, 0, 0, 0);
  }
  if(!(offset > stops.front().m_offset)) {
    return stops.front().m_color;
  }
  auto previous = stops.front();
  for(auto& stop : stops) {
    auto position = std::max(stop.m_offset, previous.m_offset);
    if(offset <= position) {
      auto span = position - previous.m_offset;
      if(!(span > 0)) {
        return stop.m_color;
      }
      auto t = (offset - previous.m_offset) / span;
      auto& start = previous.m_color;
      auto& end = stop.m_color;
      return Color(start.m_red + t * (end.m_red - start.m_red),
        start.m_green + t * (end.m_green - start.m_green),
        start.m_blue + t * (end.m_blue - start.m_blue),
        start.m_alpha + t * (end.m_alpha - start.m_alpha));
    }
    previous = GradientStop(position, stop.m_color);
  }
  return stops.back().m_color;
}

double Rellu::get_radial_offset(const RadialGradient& gradient, Point point) {
  if(!(gradient.m_radius > 0)) {
    return 1;
  }
  auto focus = gradient.m_focus;
  auto distance = length(focus - gradient.m_center);
  auto limit = 0.999 * gradient.m_radius;
  if(distance > limit) {
    focus = gradient.m_center +
      (focus - gradient.m_center) * (limit / distance);
  }
  auto position = point - focus;
  auto direction = gradient.m_center - focus;
  auto a = direction.m_x * direction.m_x + direction.m_y * direction.m_y -
    gradient.m_radius * gradient.m_radius;
  auto b = position.m_x * direction.m_x + position.m_y * direction.m_y;
  auto c = position.m_x * position.m_x + position.m_y * position.m_y;
  return (b - std::sqrt(std::max(0.0, b * b - a * c))) / a;
}

int Rellu::get_blur_downsampling(double radius) {
  auto factor = 1;
  while(factor < MAXIMUM_DOWNSAMPLING && radius / (2 * factor) >= 2) {
    factor *= 2;
  }
  return factor;
}
