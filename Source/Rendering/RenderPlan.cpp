#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <utility>
#include "Rellu/Rendering/RenderPlan.hpp"

using namespace Rellu;

namespace {
  constexpr auto MINIMUM_RADIUS = 0.25;
  constexpr auto MAXIMUM_DOWNSAMPLING = 32;
  constexpr auto MAXIMUM_TAPS = 32;
  constexpr auto MAXIMUM_BOX_SIZE = 2 * MAXIMUM_TAPS;
  constexpr auto TEXTURE_GRANULARITY = 32;
  constexpr auto SOLID_RUN = 256u;
  constexpr auto MAXIMUM_COORDINATE = 1 << 24;
  constexpr auto INNER_SHADOW_MODE = 16.0f;
  constexpr auto EXACT_BOX_KERNEL = 100.0f;
  constexpr auto SPLINE_BOX_KERNEL = 200.0f;
  constexpr auto ATLAS_SIZE = 2048;
  constexpr auto MAXIMUM_CELL = 1024;
  constexpr auto MAXIMUM_PIECES = std::size_t(16);
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

  struct Range {
    std::uint32_t m_first;
    std::uint32_t m_count;
  };

  struct Piece {
    QRect m_bounds;
    std::vector<Range> m_ranges;
  };

  struct Surface {
    std::size_t m_command;
    QRect m_bounds;
    QRect m_region;
    std::vector<Operation> m_operations;
    std::vector<Piece> m_pieces;
    bool m_is_whole = false;
  };

  struct Target {
    int m_texture;
    QPoint m_origin;
    double m_scale;
    QSize m_size;
    QRect m_cell;

    QSizeF get_extent() const {
      return QSizeF(m_size) * m_scale;
    }

    QRect get_area() const {
      if(m_cell.isEmpty()) {
        return QRect(m_origin, get_extent().toSize());
      }
      return QRect(
        m_origin + m_cell.topLeft() * m_scale, m_cell.size() * m_scale);
    }
  };

  struct Prepared {
    QRect m_region;
    Target m_target;
  };

  struct Cell {
    std::size_t m_layer;
    QRect m_region;
    std::vector<Range> m_ranges;
  };

  struct BlurTask {
    Target m_source;
    QRect m_region;
    double m_radius;
    Target m_result;
  };

  class Packer {
    public:
      Packer(int width, int height)
        : m_width(width),
          m_height(height),
          m_shelf(0),
          m_size(0, 0) {}

      std::optional<QPoint> add(QSize size) {
        if(m_cursor.x() + size.width() > m_width) {
          m_cursor = QPoint(0, m_cursor.y() + m_shelf);
          m_shelf = 0;
        }
        if(m_cursor.y() + size.height() > m_height) {
          return std::nullopt;
        }
        auto position = m_cursor;
        m_cursor.rx() += size.width();
        m_shelf = std::max(m_shelf, size.height());
        m_size = m_size.expandedTo(
          QSize(m_cursor.x(), m_cursor.y() + m_shelf));
        return position;
      }

      QSize get_size() const {
        return m_size;
      }

    private:
      int m_width;
      int m_height;
      QPoint m_cursor;
      int m_shelf;
      QSize m_size;
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

  int get_box_size(double radius) {
    return static_cast<int>(std::floor(
      radius * 3 * std::sqrt(2 * std::numbers::pi) / 4 + 0.5));
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

  Projection get_projection(const Matrix& view, const FillCommand& command,
      const Matrix& transform, const std::optional<Projection>& projection) {
    if(projection) {
      return view * command.m_transform * *projection * transform;
    }
    return to_projection(view * command.m_transform * transform);
  }

  Texel to_divisor(const Projection& projection) {
    return Texel({static_cast<float>(projection.m_wx),
      static_cast<float>(projection.m_wy),
      static_cast<float>(projection.m_w0 - 1), 0});
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
      std::vector<std::vector<Prepared>> m_prepared;

      const DrawCommand& get_command(std::size_t index) const;
      const BeginLayerCommand& get_layer(std::size_t surface) const;
      void add_paints();
      void add_gradient(int slot, const FillCommand& command,
        const GradientPaint& paint);
      void add_image(int slot, const FillCommand& command,
        const ImagePaint& paint,
        std::unordered_map<const QImage*, int>& images);
      void add_surfaces();
      void add_strips(Surface& surface, std::uint32_t first,
        std::uint32_t count);
      void add_piece(Surface& surface, const QRect& bounds,
        std::uint32_t first, std::uint32_t count);
      void close(std::vector<std::size_t>& stack);
      QRect get_region(const Surface& surface,
        const BeginLayerCommand& layer) const;
      QRect get_open_region(const QRect& region) const;
      QRect get_bounds(std::uint32_t first, std::uint32_t count) const;
      std::uint32_t get_first(std::size_t command) const;
      std::uint32_t get_count(std::size_t command) const;
      int round(int value) const;
      QSize get_bucket(QSize size) const;
      Target allocate(QSize size, QPoint origin, double scale);
      Target allocate_texture(QSize size, QPoint origin, double scale);
      Target allocate_cells(const std::vector<QRect>& regions, int scale,
        std::vector<Target>& cells);
      void release(const Target& target);
      PlanUniforms make_uniforms(const Target& target) const;
      int add_uniforms(const Target& target);
      std::size_t add_pass(const Target& target, PlanClear clear);
      PlanPass& get_pass(std::size_t index);
      void draw(std::size_t pass, PlanPipeline pipeline, std::uint32_t first,
        std::uint32_t count, PlanSource source, int backdrop = -1,
        int uniforms = -1, const QRect& scissor = QRect());
      void draw_strips(std::size_t pass, std::uint32_t first,
        std::uint32_t count, int uniforms = -1,
        const QRect& scissor = QRect());
      std::uint32_t add_span(
        const QRect& rect, const std::array<qfloat16, 4>& color, int slot);
      int add_paint();
      Texel get_limits(const Target& target) const;
      void set_texture_paint(int slot, const Target& target, Point offset,
        PaintKind kind, const Target* backdrop = nullptr,
        float mode = 0);
      void set_shadow_paint(int slot, const Target& source,
        const QRect& region, Point offset);
      void emit_content(std::size_t surface, const Target& target,
        PlanClear clear);
      Target emit_layer(std::size_t surface);
      bool is_piecewise(std::size_t surface) const;
      std::vector<Cell> get_cells(std::size_t surface) const;
      bool is_batchable(std::size_t surface) const;
      void prepare_batch(std::size_t surface, std::size_t start,
        std::vector<Target>& atlases);
      Target blur(const Target& source, const QRect& region, double radius,
        bool is_inverted, double brightness);
      void blur(std::vector<BlurTask>& tasks, std::vector<Target>& atlases);
      Target blur_boxes(
        const Target& source, const QRect& region, double radius);
      void set_blur_kernel(int block, double radius, int level);
      void add_blur_pass(const Target& source, QSize texels,
        const Target& destination, Point step, double radius,
        bool is_inverted, double brightness);
      void add_blur_draw(std::size_t pass, int uniforms,
        const Target& source, QSize texels, const Target& destination,
        Point step, double radius, bool is_inverted, double brightness);
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
    m_prepared.assign(m_surfaces.size(), std::vector<Prepared>());
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

  const BeginLayerCommand& Planner::get_layer(std::size_t surface) const {
    return std::get<BeginLayerCommand>(
      get_command(m_surfaces[surface].m_command));
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
    auto inverse = invert(get_projection(
      m_view, command, paint.m_transform, paint.m_projection));
    auto constant = [&] {
      record[0] = Texel({0, 0, 1, to_float(PaintKind::LINEAR)});
      record[4] = Texel();
    };
    if(inverse) {
      record[4] = to_divisor(*inverse);
    }
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
        auto start = dot(direction, linear->m_start);
        record[0] = Texel({
          static_cast<float>((direction.m_x * matrix.m_a +
            direction.m_y * matrix.m_b - start * matrix.m_wx) / length),
          static_cast<float>((direction.m_x * matrix.m_c +
            direction.m_y * matrix.m_d - start * matrix.m_wy) / length),
          static_cast<float>((direction.m_x * matrix.m_tx +
            direction.m_y * matrix.m_ty - start * matrix.m_w0) / length),
          to_float(PaintKind::LINEAR)});
      }
    } else if(auto angular =
        std::get_if<AngularGradient>(&paint.m_geometry)) {
      auto& matrix = *inverse;
      record[0] = Texel({static_cast<float>(matrix.m_a),
        static_cast<float>(matrix.m_c), static_cast<float>(matrix.m_tx),
        to_float(PaintKind::ANGULAR)});
      record[1] = Texel({static_cast<float>(matrix.m_b),
        static_cast<float>(matrix.m_d), static_cast<float>(matrix.m_ty), 0});
      record[2] = Texel({static_cast<float>(angular->m_center.m_x),
        static_cast<float>(angular->m_center.m_y),
        static_cast<float>(angular->m_rotation / 360), 0});
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
    auto projection = get_projection(
      m_view, command, paint.m_transform, paint.m_projection);
    auto inverse = invert(projection);
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
      static_cast<float>(matrix.m_ty / height), 1});
    auto image_bounds = paint.m_transform.map(Rect(0, 0, width, height));
    if(paint.m_projection) {
      image_bounds = (*paint.m_projection * paint.m_transform).map(
        Rect(0, 0, width, height));
    }
    auto margin = 1e-3 * std::max(image_bounds.m_width, image_bounds.m_height);
    auto path_bounds = Rellu::get_bounds(*command.m_path);
    if(!inflate(image_bounds, margin).contains(path_bounds)) {
      record[2] = Texel({0, 1, 0, 0});
    }
    record[3] =
      Texel({static_cast<float>(width), static_cast<float>(height), 0, 0});
    record[4] = to_divisor(matrix);
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
          surface.m_pieces.clear();
          surface.m_is_whole = true;
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
      auto bounds = get_bounds(first, count);
      surface.m_bounds |= bounds;
      add_piece(surface, bounds, first, count);
    }
  }

  void Planner::add_piece(Surface& surface, const QRect& bounds,
      std::uint32_t first, std::uint32_t count) {
    if(surface.m_is_whole) {
      return;
    }
    auto piece = Piece(bounds, std::vector<Range>({Range(first, count)}));
    auto& pieces = surface.m_pieces;
    auto is_merged = true;
    while(is_merged) {
      is_merged = false;
      auto area = piece.m_bounds.adjusted(-1, -1, 1, 1);
      for(auto i = pieces.begin(); i != pieces.end(); ++i) {
        if(area.intersects(i->m_bounds.adjusted(-1, -1, 1, 1))) {
          piece.m_bounds |= i->m_bounds;
          piece.m_ranges.insert(piece.m_ranges.end(), i->m_ranges.begin(),
            i->m_ranges.end());
          pieces.erase(i);
          is_merged = true;
          break;
        }
      }
    }
    std::sort(piece.m_ranges.begin(), piece.m_ranges.end(),
      [] (const auto& left, const auto& right) {
        return left.m_first < right.m_first;
      });
    pieces.push_back(std::move(piece));
    if(pieces.size() > MAXIMUM_PIECES) {
      pieces.clear();
      surface.m_is_whole = true;
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
      m_surfaces[parent].m_pieces.clear();
      m_surfaces[parent].m_is_whole = true;
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
      region |= bounds.adjusted(-margin, -margin, margin, margin);
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

  QRect Planner::get_open_region(const QRect& region) const {
    constexpr auto OPEN = MAXIMUM_COORDINATE;
    auto open = region;
    if(region.left() <= 0) {
      open.setLeft(-OPEN);
    }
    if(region.top() <= 0) {
      open.setTop(-OPEN);
    }
    if(region.right() >= m_size.width() - 1) {
      open.setRight(OPEN);
    }
    if(region.bottom() >= m_size.height() - 1) {
      open.setBottom(OPEN);
    }
    return open;
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

  int Planner::round(int value) const {
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
  }

  QSize Planner::get_bucket(QSize size) const {
    return QSize(round(size.width()), round(size.height()));
  }

  Target Planner::allocate(QSize size, QPoint origin, double scale) {
    return allocate_texture(get_bucket(size), origin, scale);
  }

  Target Planner::allocate_texture(
      QSize size, QPoint origin, double scale) {
    auto& textures = m_plan->m_textures;
    auto match = std::find_if(m_free.begin(), m_free.end(), [&] (auto index) {
      return textures[index] == size;
    });
    auto texture = 0;
    if(match != m_free.end()) {
      texture = *match;
      m_free.erase(match);
    } else {
      texture = static_cast<int>(textures.size());
      textures.push_back(size);
    }
    return Target(texture, origin, scale, size);
  }

  Target Planner::allocate_cells(const std::vector<QRect>& regions,
      int scale, std::vector<Target>& cells) {
    auto packer = Packer(ATLAS_SIZE, std::numeric_limits<int>::max());
    auto placements = std::vector<QRect>();
    for(auto& region : regions) {
      auto size = get_bucket(QSize((region.width() + scale - 1) / scale,
        (region.height() + scale - 1) / scale));
      placements.push_back(QRect(*packer.add(size), size));
    }
    auto to_power = [] (int value) {
      return static_cast<int>(std::bit_ceil(static_cast<unsigned int>(
        std::max(value, TEXTURE_GRANULARITY))));
    };
    auto atlas = allocate_texture(QSize(to_power(packer.get_size().width()),
      to_power(packer.get_size().height())), QPoint(0, 0), scale);
    cells.clear();
    for(auto i = std::size_t(0); i != regions.size(); ++i) {
      auto& placement = placements[i];
      cells.push_back(Target(atlas.m_texture,
        regions[i].topLeft() - placement.topLeft() * scale, scale,
        atlas.m_size, placement));
    }
    return atlas;
  }

  void Planner::release(const Target& target) {
    if(target.m_texture >= 0) {
      m_free.push_back(target.m_texture);
    }
  }

  PlanUniforms Planner::make_uniforms(const Target& target) const {
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
    return uniforms;
  }

  int Planner::add_uniforms(const Target& target) {
    m_plan->m_uniforms.push_back(make_uniforms(target));
    return static_cast<int>(m_plan->m_uniforms.size() - 1);
  }

  std::size_t Planner::add_pass(const Target& target, PlanClear clear) {
    auto& steps = m_plan->m_steps;
    auto uniforms = make_uniforms(target);
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
      int backdrop, int uniforms, const QRect& scissor) {
    if(count == 0) {
      return;
    }
    auto& draws = get_pass(pass).m_draws;
    if(!draws.empty()) {
      auto& last = draws.back();
      if(last.m_pipeline == pipeline && last.m_source == source &&
          last.m_backdrop == backdrop && last.m_uniforms == uniforms &&
          last.m_scissor == scissor && last.m_first + last.m_count == first) {
        last.m_count += count;
        return;
      }
    }
    draws.push_back(PlanDraw(
      pipeline, first, count, source, backdrop, uniforms, scissor));
  }

  void Planner::draw_strips(std::size_t pass, std::uint32_t first,
      std::uint32_t count, int uniforms, const QRect& scissor) {
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
            PlanSource(image, -1), -1, uniforms, scissor);
          start = painted + 1;
          pipeline = PlanPipeline::SOLID;
          image = -1;
        }
        continue;
      }
      auto needed = m_slot_images[static_cast<std::size_t>(paint)];
      if(pipeline == PlanPipeline::SOLID) {
        if(i - start > SOLID_RUN) {
          draw(pass, pipeline, start, i - start, PlanSource(), -1, uniforms,
            scissor);
          start = i;
        }
        pipeline = PlanPipeline::OVER;
      } else if(needed >= 0 && image >= 0 && needed != image) {
        draw(pass, pipeline, start, i - start, PlanSource(image, -1), -1,
          uniforms, scissor);
        start = i;
      }
      if(needed >= 0) {
        image = needed;
      }
      painted = i;
    }
    draw(pass, pipeline, start, last - start, PlanSource(image, -1), -1,
      uniforms, scissor);
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

  Texel Planner::get_limits(const Target& target) const {
    if(target.m_cell.isEmpty()) {
      return Texel();
    }
    auto width = static_cast<double>(target.m_size.width());
    auto height = static_cast<double>(target.m_size.height());
    auto& cell = target.m_cell;
    auto left = (cell.x() + 0.5) / width;
    auto right = (cell.x() + cell.width() - 0.5) / width;
    auto top = (cell.y() + 0.5) / height;
    auto bottom = (cell.y() + cell.height() - 0.5) / height;
    if(m_options->m_is_y_up) {
      auto flipped = 1 - bottom;
      bottom = 1 - top;
      top = flipped;
    }
    return Texel({static_cast<float>(left), static_cast<float>(top),
      static_cast<float>(right), static_cast<float>(bottom)});
  }

  void Planner::set_texture_paint(int slot, const Target& target,
      Point offset, PaintKind kind, const Target* backdrop, float mode) {
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
    record[1] = Texel({0, source[2], source[3], mode});
    if(backdrop) {
      record[2] = map(*backdrop, Point(0, 0));
    }
    record[3] = Texel({static_cast<float>(target.m_size.width()),
      static_cast<float>(target.m_size.height()), 0, 0});
    record[5] = get_limits(target);
    std::copy(record.begin(), record.end(),
      m_plan->m_paints.begin() + PAINT_TEXELS * slot);
  }

  void Planner::set_shadow_paint(int slot, const Target& source,
      const QRect& region, Point offset) {
    auto open = get_open_region(region);
    auto bounds = Target(-1, open.topLeft() + QPoint(
      static_cast<int>(std::lround(offset.m_x)),
      static_cast<int>(std::lround(offset.m_y))), 1, open.size());
    set_texture_paint(
      slot, source, offset, PaintKind::TEXTURE_ALPHA, &bounds);
  }

  void Planner::emit_content(
      std::size_t index, const Target& target, PlanClear clear) {
    auto pass = std::optional<std::size_t>();
    auto open = [&] {
      pass = add_pass(target, pass ? PlanClear::KEEP : clear);
      return *pass;
    };
    auto atlases = std::vector<Target>();
    auto release_atlases = [&] {
      for(auto& atlas : atlases) {
        release(atlas);
      }
      atlases.clear();
    };
    auto& operations = m_surfaces[index].m_operations;
    for(auto i = std::size_t(0); i != operations.size(); ++i) {
      auto& operation = operations[i];
      if(operation.m_type == OperationType::STRIPS) {
        draw_strips(open(), operation.m_first, operation.m_count);
      } else if(operation.m_type == OperationType::LAYER) {
        auto& child = m_surfaces[operation.m_index];
        auto& layer = get_layer(operation.m_index);
        auto is_batched = is_batchable(operation.m_index);
        if(is_batched && m_prepared[operation.m_index].empty()) {
          release_atlases();
          prepare_batch(index, i, atlases);
        }
        if(is_batched && is_piecewise(operation.m_index)) {
          auto& pieces = m_prepared[operation.m_index];
          auto backdrops = std::vector<Target>();
          if(layer.m_blend_mode != BlendMode::NORMAL) {
            if(!pass) {
              open();
            }
            for(auto& piece : pieces) {
              auto& region = piece.m_region;
              backdrops.push_back(
                allocate(region.size(), region.topLeft(), 1));
              add_copy(target, region, backdrops.back());
            }
          }
          for(auto j = std::size_t(0); j != pieces.size(); ++j) {
            auto& piece = pieces[j];
            auto slot = add_paint();
            auto span = add_span(piece.m_region,
              to_premultiplied(Color(1, 1, 1, layer.m_opacity)), slot);
            if(backdrops.empty()) {
              set_texture_paint(
                slot, piece.m_target, Point(0, 0), PaintKind::TEXTURE);
              draw(open(), PlanPipeline::OVER, span, 1,
                PlanSource(-1, piece.m_target.m_texture));
            } else {
              set_texture_paint(slot, piece.m_target, Point(0, 0),
                PaintKind::TEXTURE, &backdrops[j],
                static_cast<float>(layer.m_blend_mode));
              draw(open(), PlanPipeline::BLEND, span, 1,
                PlanSource(-1, piece.m_target.m_texture),
                backdrops[j].m_texture);
            }
          }
          for(auto& backdrop : backdrops) {
            release(backdrop);
          }
          continue;
        }
        auto source = Target();
        if(is_batched) {
          source = m_prepared[operation.m_index].front().m_target;
        } else {
          source = emit_layer(operation.m_index);
        }
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
          draw(open(), PlanPipeline::OVER, first, count,
            PlanSource(-1, source.m_texture));
        } else {
          if(!pass) {
            open();
          }
          auto& region = child.m_region;
          auto backdrop = allocate(region.size(), region.topLeft(), 1);
          add_copy(target, region, backdrop);
          set_texture_paint(slot, source, Point(0, 0), PaintKind::TEXTURE,
            &backdrop, static_cast<float>(layer.m_blend_mode));
          draw(open(), PlanPipeline::BLEND, first, count,
            PlanSource(-1, source.m_texture), backdrop.m_texture);
          release(backdrop);
        }
        if(!is_batched) {
          release(source);
        }
      } else {
        auto& command =
          std::get<BackdropBlurCommand>(get_command(operation.m_index));
        auto radius = command.m_radius * m_scale;
        auto region = round_out(Rellu::get_bounds(
          *command.m_path, m_view * command.m_transform)) &
          get_bounds(operation.m_first, operation.m_count) &
          QRect(target.m_origin, target.get_extent().toSize());
        if(index == 0) {
          region &= QRect(QPoint(0, 0), m_size);
        }
        if(region.isEmpty()) {
          continue;
        }
        if(!pass) {
          open();
        }
        auto backdrop = allocate(region.size(), region.topLeft(), 1);
        add_copy(target, region, backdrop);
        if(command.m_brightness != 0) {
          auto brightened = allocate(region.size(), region.topLeft(), 1);
          add_blur_pass(backdrop, region.size(), brightened, Point(0, 0), 0,
            false, command.m_brightness);
          release(backdrop);
          backdrop = brightened;
        }
        auto blurred = blur_boxes(backdrop, region, radius);
        release(backdrop);
        auto slot = m_command_slots[operation.m_index];
        set_texture_paint(slot, blurred, Point(0, 0), PaintKind::TEXTURE);
        draw(open(), PlanPipeline::OVER, operation.m_first, operation.m_count,
          PlanSource(-1, blurred.m_texture));
        release(blurred);
      }
    }
    if(!pass) {
      open();
    }
    release_atlases();
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
      auto backdrop = allocate(region.size(), region.topLeft(), 1);
      add_copy(target, region, backdrop);
      auto slot = add_paint();
      set_texture_paint(slot, blurred, map_vector(m_view, shadow.m_offset),
        PaintKind::TEXTURE, &backdrop, INNER_SHADOW_MODE);
      auto span = add_span(region, to_premultiplied(shadow.m_color), slot);
      auto pass = add_pass(target, PlanClear::KEEP);
      draw(pass, PlanPipeline::BLEND, span, 1,
        PlanSource(-1, blurred.m_texture), backdrop.m_texture);
      release(backdrop);
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
        set_shadow_paint(
          slot, source, region, map_vector(m_view, shadow.m_offset));
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

  bool Planner::is_piecewise(std::size_t index) const {
    auto& surface = m_surfaces[index];
    auto& layer = get_layer(index);
    return !surface.m_is_whole && surface.m_pieces.size() > 1 &&
      layer.m_drop_shadows.empty() && layer.m_inner_shadows.empty() &&
      !(layer.m_blur * m_scale >= MINIMUM_RADIUS) && !layer.m_mask;
  }

  std::vector<Cell> Planner::get_cells(std::size_t index) const {
    auto& surface = m_surfaces[index];
    auto cells = std::vector<Cell>();
    if(is_piecewise(index)) {
      for(auto& piece : surface.m_pieces) {
        auto region =
          piece.m_bounds.adjusted(-1, -1, 1, 1) & surface.m_region;
        if(!region.isEmpty()) {
          cells.push_back(Cell(index, region, piece.m_ranges));
        }
      }
      return cells;
    }
    auto ranges = std::vector<Range>();
    for(auto& operation : surface.m_operations) {
      ranges.push_back(Range(operation.m_first, operation.m_count));
    }
    cells.push_back(Cell(index, surface.m_region, std::move(ranges)));
    return cells;
  }

  bool Planner::is_batchable(std::size_t index) const {
    auto& surface = m_surfaces[index];
    if(!get_layer(index).m_inner_shadows.empty() ||
        !std::all_of(surface.m_operations.begin(),
          surface.m_operations.end(), [] (const auto& operation) {
            return operation.m_type == OperationType::STRIPS;
          })) {
      return false;
    }
    auto is_small = [&] (const QRect& region) {
      auto bucket = get_bucket(region.size());
      return bucket.width() <= MAXIMUM_CELL && bucket.height() <= MAXIMUM_CELL;
    };
    if(is_piecewise(index)) {
      auto cells = get_cells(index);
      auto packer = Packer(ATLAS_SIZE, ATLAS_SIZE);
      return !cells.empty() && std::all_of(cells.begin(), cells.end(),
        [&] (const auto& cell) {
          return is_small(cell.m_region) &&
            packer.add(get_bucket(cell.m_region.size()));
        });
    }
    return is_small(surface.m_region);
  }

  void Planner::prepare_batch(std::size_t index, std::size_t start,
      std::vector<Target>& atlases) {
    auto& operations = m_surfaces[index].m_operations;
    auto packer = Packer(ATLAS_SIZE, ATLAS_SIZE);
    auto cells = std::vector<Cell>();
    for(auto i = start; i != operations.size(); ++i) {
      auto& operation = operations[i];
      if(operation.m_type != OperationType::LAYER ||
          !is_batchable(operation.m_index)) {
        continue;
      }
      auto layer_cells = get_cells(operation.m_index);
      auto is_full = false;
      for(auto& cell : layer_cells) {
        if(!packer.add(get_bucket(cell.m_region.size()))) {
          is_full = true;
          break;
        }
      }
      if(is_full) {
        break;
      }
      cells.insert(cells.end(), layer_cells.begin(), layer_cells.end());
    }
    auto cell_regions = std::vector<QRect>();
    for(auto& cell : cells) {
      cell_regions.push_back(cell.m_region);
    }
    auto cell_targets = std::vector<Target>();
    atlases.push_back(allocate_cells(cell_regions, 1, cell_targets));
    auto pass = add_pass(atlases.back(), PlanClear::CLEAR);
    for(auto i = std::size_t(0); i != cells.size(); ++i) {
      auto uniforms = add_uniforms(cell_targets[i]);
      for(auto& range : cells[i].m_ranges) {
        draw_strips(pass, range.m_first, range.m_count, uniforms,
          cell_targets[i].m_cell);
      }
    }
    auto layers = std::vector<std::size_t>();
    auto regions = std::vector<QRect>();
    auto targets = std::vector<Target>();
    for(auto i = std::size_t(0); i != cells.size(); ++i) {
      auto& prepared = m_prepared[cells[i].m_layer];
      prepared.push_back(Prepared(cells[i].m_region, cell_targets[i]));
      if(!is_piecewise(cells[i].m_layer)) {
        layers.push_back(cells[i].m_layer);
        regions.push_back(cells[i].m_region);
        targets.push_back(cell_targets[i]);
      }
    }
    auto tasks = std::vector<BlurTask>();
    auto shadowed = std::vector<std::size_t>();
    for(auto i = std::size_t(0); i != layers.size(); ++i) {
      auto& shadows = get_layer(layers[i]).m_drop_shadows;
      if(shadows.empty()) {
        continue;
      }
      shadowed.push_back(i);
      for(auto& shadow : shadows) {
        auto radius = shadow.m_radius * m_scale;
        if(radius >= MINIMUM_RADIUS) {
          tasks.push_back(BlurTask(targets[i], regions[i], radius, Target()));
        }
      }
    }
    if(!shadowed.empty()) {
      auto blurred = std::vector<Target>();
      blur(tasks, blurred);
      auto composite_regions = std::vector<QRect>();
      for(auto i : shadowed) {
        composite_regions.push_back(regions[i]);
      }
      auto composites = std::vector<Target>();
      atlases.push_back(allocate_cells(composite_regions, 1, composites));
      auto composite_pass = add_pass(atlases.back(), PlanClear::CLEAR);
      auto task = tasks.begin();
      for(auto j = std::size_t(0); j != shadowed.size(); ++j) {
        auto i = shadowed[j];
        auto uniforms = add_uniforms(composites[j]);
        for(auto& shadow : get_layer(layers[i]).m_drop_shadows) {
          auto source = targets[i];
          if(shadow.m_radius * m_scale >= MINIMUM_RADIUS) {
            source = task->m_result;
            ++task;
          }
          auto slot = add_paint();
          set_shadow_paint(
            slot, source, regions[i], map_vector(m_view, shadow.m_offset));
          auto span =
            add_span(regions[i], to_premultiplied(shadow.m_color), slot);
          draw(composite_pass, PlanPipeline::OVER, span, 1,
            PlanSource(-1, source.m_texture), -1, uniforms);
        }
        auto slot = add_paint();
        set_texture_paint(slot, targets[i], Point(0, 0), PaintKind::TEXTURE);
        auto span =
          add_span(regions[i], to_premultiplied(Color(1, 1, 1, 1)), slot);
        draw(composite_pass, PlanPipeline::OVER, span, 1,
          PlanSource(-1, targets[i].m_texture), -1, uniforms);
        targets[i] = composites[j];
      }
      for(auto& atlas : blurred) {
        release(atlas);
      }
    }
    tasks.clear();
    auto blurred_layers = std::vector<std::size_t>();
    for(auto i = std::size_t(0); i != layers.size(); ++i) {
      auto radius = get_layer(layers[i]).m_blur * m_scale;
      if(radius >= MINIMUM_RADIUS) {
        tasks.push_back(BlurTask(targets[i], regions[i], radius, Target()));
        blurred_layers.push_back(i);
      }
    }
    blur(tasks, atlases);
    for(auto j = std::size_t(0); j != tasks.size(); ++j) {
      targets[blurred_layers[j]] = tasks[j].m_result;
    }
    for(auto i = std::size_t(0); i != layers.size(); ++i) {
      m_prepared[layers[i]].front().m_target = targets[i];
    }
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
      if(factor % (2 * level) == 0) {
        level *= 2;
      } else {
        level = factor;
      }
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
    auto horizontal = allocate(get_size(level), region.topLeft(), level);
    auto pass = add_pass(horizontal, PlanClear::CLEAR);
    add_blur_draw(pass, -1, current, get_size(level), horizontal,
      Point(1.0 / current.m_size.width(), 0), 0, is_first && is_inverted, 0);
    set_blur_kernel(get_pass(pass).m_uniforms, radius, level);
    if(current.m_texture != source.m_texture) {
      release(current);
    }
    auto vertical = allocate(get_size(level), region.topLeft(), level);
    pass = add_pass(vertical, PlanClear::CLEAR);
    add_blur_draw(pass, -1, horizontal, get_size(level), vertical,
      Point(0, 1.0 / horizontal.m_size.height()), 0, false, brightness);
    set_blur_kernel(get_pass(pass).m_uniforms, radius, level);
    release(horizontal);
    return vertical;
  }

  void Planner::blur(
      std::vector<BlurTask>& tasks, std::vector<Target>& atlases) {
    auto get_size = [] (const QRect& region, int scale) {
      return QSize((region.width() + scale - 1) / scale,
        (region.height() + scale - 1) / scale);
    };
    auto factors = std::vector<int>();
    for(auto& task : tasks) {
      auto factor = get_blur_downsampling(task.m_radius);
      if(std::find(factors.begin(), factors.end(), factor) == factors.end()) {
        factors.push_back(factor);
      }
    }
    for(auto factor : factors) {
      auto group = std::vector<std::size_t>();
      auto regions = std::vector<QRect>();
      auto current = std::vector<Target>();
      for(auto i = std::size_t(0); i != tasks.size(); ++i) {
        if(get_blur_downsampling(tasks[i].m_radius) == factor) {
          group.push_back(i);
          regions.push_back(tasks[i].m_region);
          current.push_back(tasks[i].m_source);
        }
      }
      auto intermediate = std::optional<Target>();
      auto level = 1;
      while(level < factor) {
        level *= 2;
        auto destinations = std::vector<Target>();
        auto atlas = allocate_cells(regions, level, destinations);
        auto pass = add_pass(atlas, PlanClear::CLEAR);
        for(auto k = std::size_t(0); k != group.size(); ++k) {
          add_blur_draw(pass, add_uniforms(destinations[k]), current[k],
            get_size(regions[k], level / 2), destinations[k], Point(0, 0), 0,
            false, 0);
        }
        if(intermediate) {
          release(*intermediate);
        }
        intermediate = atlas;
        current = std::move(destinations);
      }
      auto horizontals = std::vector<Target>();
      auto horizontal = allocate_cells(regions, level, horizontals);
      auto pass = add_pass(horizontal, PlanClear::CLEAR);
      for(auto k = std::size_t(0); k != group.size(); ++k) {
        auto uniforms = add_uniforms(horizontals[k]);
        add_blur_draw(pass, uniforms, current[k],
          get_size(regions[k], level), horizontals[k],
          Point(1.0 / current[k].m_size.width(), 0), 0, false, 0);
        set_blur_kernel(uniforms, tasks[group[k]].m_radius, level);
      }
      if(intermediate) {
        release(*intermediate);
      }
      auto verticals = std::vector<Target>();
      atlases.push_back(allocate_cells(regions, level, verticals));
      pass = add_pass(atlases.back(), PlanClear::CLEAR);
      for(auto k = std::size_t(0); k != group.size(); ++k) {
        auto uniforms = add_uniforms(verticals[k]);
        add_blur_draw(pass, uniforms, horizontals[k],
          get_size(regions[k], level), verticals[k],
          Point(0, 1.0 / horizontals[k].m_size.height()), 0, false, 0);
        set_blur_kernel(uniforms, tasks[group[k]].m_radius, level);
        tasks[group[k]].m_result = verticals[k];
      }
      release(horizontal);
    }
  }

  void Planner::set_blur_kernel(int block, double radius, int level) {
    auto& uniforms = m_plan->m_uniforms[block];
    auto size = std::max(1, get_box_size(radius));
    auto half = size / 2;
    if(level == 1) {
      uniforms[26] = static_cast<float>(size);
      uniforms[27] = EXACT_BOX_KERNEL +
        static_cast<float>(std::min(MAXIMUM_TAPS, 3 * half));
      return;
    }
    auto square = static_cast<double>(size) * size;
    auto variance = 3 * (square - 1) / 12;
    if(size % 2 == 0) {
      variance = (2 * (square - 1) + (square + 2 * size)) / 12;
    }
    variance =
      std::max(0.01, variance - (static_cast<double>(level) * level - 1) / 6);
    auto width = 2 * std::sqrt(variance) / level;
    uniforms[26] = static_cast<float>(width);
    uniforms[27] = SPLINE_BOX_KERNEL + static_cast<float>(
      std::min(MAXIMUM_TAPS, to_coordinate(std::ceil(1.5 * width))));
  }

  Target Planner::blur_boxes(
      const Target& source, const QRect& region, double radius) {
    auto resolution = std::max(1.0, m_scale);
    auto factor = 1;
    while(factor < MAXIMUM_DOWNSAMPLING &&
        get_box_size(radius / (resolution * factor)) > MAXIMUM_BOX_SIZE) {
      factor *= 2;
    }
    auto get_size = [] (const QRect& area, double scale) {
      return QSize(static_cast<int>(std::ceil(area.width() / scale - 1e-9)),
        static_cast<int>(std::ceil(area.height() / scale - 1e-9)));
    };
    auto current = source;
    auto texels = region.size();
    auto level = 1.0;
    while(level < resolution * factor) {
      if(level == 1 && resolution > 1) {
        level = resolution;
      } else {
        level *= 2;
      }
      auto destination =
        allocate(get_size(region, level), region.topLeft(), level);
      add_blur_pass(current, texels, destination, Point(0, 0), 0, false, 0);
      if(current.m_texture != source.m_texture) {
        release(current);
      }
      current = destination;
      texels = get_size(region, level);
    }
    auto size = std::max(1, get_box_size(radius / level));
    auto half = size / 2;
    auto windows = std::array<std::pair<int, int>, 3>();
    if(size % 2 == 1) {
      windows.fill(std::pair(-half, half));
    } else {
      windows = {std::pair(-half, half - 1), std::pair(-half + 1, half),
        std::pair(-half, half)};
    }
    auto margin = static_cast<int>(std::ceil((3 * half + 1) * level));
    auto padding = region.adjusted(-margin, -margin, margin, margin);
    for(auto axis = 0; axis != 2; ++axis) {
      for(auto i = std::size_t(0); i != windows.size(); ++i) {
        auto destination =
          allocate(get_size(padding, level), padding.topLeft(), level);
        auto step = Point(1.0 / current.m_size.width(), 0);
        if(axis == 1) {
          step = Point(0, 1.0 / current.m_size.height());
        }
        auto pass = add_pass(destination, PlanClear::CLEAR);
        add_blur_draw(
          pass, -1, current, texels, destination, step, 0, false, 0);
        auto& uniforms = m_plan->m_uniforms[get_pass(pass).m_uniforms];
        uniforms[26] = static_cast<float>(windows[i].first - 0.5);
        uniforms[27] = static_cast<float>(windows[i].second);
        if(current.m_texture != source.m_texture) {
          release(current);
        }
        current = destination;
        texels = get_size(padding, level);
      }
    }
    return current;
  }

  void Planner::add_blur_pass(const Target& source, QSize texels,
      const Target& destination, Point step, double radius, bool is_inverted,
      double brightness) {
    auto pass = add_pass(destination, PlanClear::CLEAR);
    add_blur_draw(pass, -1, source, texels, destination, step, radius,
      is_inverted, brightness);
  }

  void Planner::add_blur_draw(std::size_t pass, int index,
      const Target& source, QSize texels, const Target& destination,
      Point step, double radius, bool is_inverted, double brightness) {
    auto block = index;
    if(block < 0) {
      block = get_pass(pass).m_uniforms;
    }
    auto& uniforms = m_plan->m_uniforms[block];
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
    auto cell = source.m_cell.topLeft();
    auto top = (cell.y() + 0.5) / height;
    auto bottom = (cell.y() + texels.height() - 0.5) / height;
    if(m_options->m_is_y_up) {
      auto flipped = 1 - bottom;
      bottom = 1 - top;
      top = flipped;
    }
    uniforms[32] = static_cast<float>((cell.x() + 0.5) / width);
    uniforms[33] = static_cast<float>(top);
    uniforms[34] =
      static_cast<float>((cell.x() + texels.width() - 0.5) / width);
    uniforms[35] = static_cast<float>(bottom);
    auto span = add_span(destination.get_area(),
      to_premultiplied(Color(1, 1, 1, 1)), -1);
    draw(pass, PlanPipeline::BLUR, span, 1,
      PlanSource(-1, source.m_texture), -1, index);
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
  for(auto threshold : {3.0, 12.0, 28.0, 64.0, 160.0}) {
    if(radius < threshold) {
      break;
    }
    factor *= 2;
  }
  return factor;
}
