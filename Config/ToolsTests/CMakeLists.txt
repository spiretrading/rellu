file(GLOB source_files ${RELLU_SOURCE_PATH}/ToolsTests/*.cpp
  ${RELLU_SOURCE_PATH}/ToolsTests/*.hpp)
add_executable(ToolsTests ${source_files})
target_link_libraries(ToolsTests PRIVATE RelluTools)
rellu_link_dependencies(ToolsTests PRIVATE DOCTEST QT_OFFSCREEN)
add_custom_command(TARGET ToolsTests POST_BUILD COMMAND ToolsTests)
rellu_install_target(ToolsTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
file(GLOB benchmark_source_files
  ${RELLU_SOURCE_PATH}/ToolsTests/Benchmarks/*.cpp)
add_executable(ToolsBenchmarks ${benchmark_source_files})
target_link_libraries(ToolsBenchmarks PRIVATE RelluIo RelluScene RelluTools)
rellu_link_dependencies(
  ToolsBenchmarks PRIVATE QT_IMAGE_FORMATS QT_OFFSCREEN QT_WINDOWS)
rellu_install_target(ToolsBenchmarks "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
