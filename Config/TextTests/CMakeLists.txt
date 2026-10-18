file(GLOB source_files ${RELLU_SOURCE_PATH}/TextTests/*.cpp)
add_executable(TextTests ${source_files})
target_link_libraries(TextTests PRIVATE RelluText)
rellu_link_dependencies(TextTests PRIVATE DOCTEST)
target_compile_definitions(
  TextTests PRIVATE RELLU_FONTS_DIRECTORY="${TEST_FONTS_DIRECTORY}")
add_dependencies(TextTests RelluFonts)
add_custom_command(TARGET TextTests POST_BUILD COMMAND TextTests)
rellu_install_target(TextTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
