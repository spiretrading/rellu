file(GLOB source_files ${RELLU_SOURCE_PATH}/SceneTests/*.cpp)
add_executable(SceneTests ${source_files})
target_link_libraries(SceneTests PRIVATE RelluScene)
rellu_link_dependencies(SceneTests PRIVATE DOCTEST)
target_compile_definitions(
  SceneTests PRIVATE RELLU_FONTS_DIRECTORY="${TEST_FONTS_DIRECTORY}")
add_dependencies(SceneTests RelluFonts)
add_custom_command(TARGET SceneTests POST_BUILD COMMAND SceneTests)
rellu_install_target(SceneTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
