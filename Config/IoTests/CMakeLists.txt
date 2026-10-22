file(GLOB source_files ${RELLU_SOURCE_PATH}/IoTests/*.cpp)
add_executable(IoTests ${source_files})
target_link_libraries(IoTests PRIVATE RelluIo)
rellu_link_dependencies(
  IoTests PRIVATE DOCTEST QT_IMAGE_FORMATS QT_OFFSCREEN QT_WINDOWS)
target_compile_definitions(
  IoTests PRIVATE RELLU_FONTS_DIRECTORY="${TEST_FONTS_DIRECTORY}")
add_dependencies(IoTests RelluFonts)
add_custom_command(TARGET IoTests POST_BUILD COMMAND IoTests)
rellu_install_target(IoTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
