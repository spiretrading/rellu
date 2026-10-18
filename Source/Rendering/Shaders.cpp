#include <stdexcept>
#include <string>
#include <QByteArray>
#include "Rellu/Rendering/Shaders.hpp"

using namespace Rellu;

QShader Rellu::load_shader(std::string_view name) {
  auto package = find_shader_package(name);
  if(package.isEmpty()) {
    throw std::runtime_error("Missing shader " + std::string(name) + ".");
  }
  auto shader = QShader::fromSerialized(
    QByteArray::fromRawData(package.data(), package.size()));
  if(!shader.isValid()) {
    throw std::runtime_error("Invalid shader " + std::string(name) + ".");
  }
  return shader;
}
