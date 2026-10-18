#ifndef RELLU_RENDERING_SHADERS_HPP
#define RELLU_RENDERING_SHADERS_HPP
#include <string_view>
#include <QByteArrayView>
#include <rhi/qshader.h>

namespace Rellu {

  /**
   * Returns the serialized package of a shader compiled into the rendering
   * library.
   * @param name The file name of the shader's source, such as strip.vert.
   * @return The package, or an empty view if no shader has the <i>name</i>.
   */
  QByteArrayView find_shader_package(std::string_view name);

  /**
   * Loads a shader compiled into the rendering library, throwing
   * std::runtime_error if it is missing or invalid.
   * @param name The file name of the shader's source, such as strip.vert.
   * @return The shader.
   */
  QShader load_shader(std::string_view name);
}

#endif
