file(GLOB source_files ${RELLU_SOURCE_PATH}/RenderingTests/*.cpp)
add_executable(RenderingTests ${source_files})
target_include_directories(
  RenderingTests PRIVATE ${RELLU_SOURCE_PATH}/Rendering)
target_link_libraries(RenderingTests PRIVATE RelluRendering)
rellu_link_dependencies(RenderingTests PRIVATE DOCTEST QT_OFFSCREEN QT_WINDOWS)
add_custom_command(TARGET RenderingTests POST_BUILD COMMAND RenderingTests)
rellu_install_target(RenderingTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
set(benchmark_path ${RELLU_SOURCE_PATH}/RenderingTests/Benchmarks)
file(GLOB benchmark_header_files ${benchmark_path}/*.hpp)
file(GLOB benchmark_source_files ${benchmark_path}/*.cpp)
file(GLOB benchmark_shader_files ${benchmark_path}/Shaders/*.vert
  ${benchmark_path}/Shaders/*.frag)
add_executable(RenderingBenchmarks
  ${benchmark_header_files} ${benchmark_source_files})
rellu_add_shaders(RenderingBenchmarks
  HEADER ${benchmark_path}/PrototypeRenderer.hpp
  FUNCTION Rellu::find_benchmark_shader SHADERS ${benchmark_shader_files})
target_link_libraries(RenderingBenchmarks PRIVATE RelluRendering)
rellu_link_dependencies(
  RenderingBenchmarks PRIVATE CLIPPER2 QT_OFFSCREEN QT_WINDOWS)
rellu_install_target(RenderingBenchmarks "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
