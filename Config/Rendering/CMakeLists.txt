include("${CMAKE_CURRENT_LIST_DIR}/shaders.cmake")
file(GLOB_RECURSE header_files ${RELLU_INCLUDE_PATH}/Rellu/Rendering/*.hpp)
file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/Rendering/*.cpp)
file(GLOB shader_files ${RELLU_SOURCE_PATH}/Rendering/Shaders/*.vert
  ${RELLU_SOURCE_PATH}/Rendering/Shaders/*.frag)
add_library(RelluRendering STATIC ${header_files} ${source_files})
source_group("Header Files" FILES ${header_files})
rellu_add_shaders(RelluRendering HEADER Rellu/Rendering/Shaders.hpp
  FUNCTION Rellu::find_shader_package SHADERS ${shader_files})
target_link_libraries(RelluRendering PUBLIC RelluGeometry)
rellu_link_dependencies(RelluRendering PUBLIC QT QT_PRIVATE)
rellu_track_target(RelluRendering)
