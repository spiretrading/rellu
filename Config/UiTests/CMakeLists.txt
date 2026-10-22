file(GLOB source_files ${RELLU_SOURCE_PATH}/UiTests/*.cpp
  ${RELLU_SOURCE_PATH}/UiTests/*.hpp)
add_executable(UiTests ${source_files})
target_link_libraries(UiTests PRIVATE RelluUi)
rellu_link_dependencies(UiTests PRIVATE DOCTEST QT_IMAGE_FORMATS QT_OFFSCREEN)
target_compile_definitions(
  UiTests PRIVATE RELLU_FONTS_DIRECTORY="${TEST_FONTS_DIRECTORY}")
add_dependencies(UiTests RelluFonts)
add_custom_command(TARGET UiTests POST_BUILD COMMAND UiTests)
rellu_install_target(UiTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
