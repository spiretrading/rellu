file(GLOB source_files ${RELLU_SOURCE_PATH}/GoldenReport/*.cpp)
add_executable(GoldenReport ${source_files})
target_link_libraries(GoldenReport PRIVATE RelluIo RelluScene)
rellu_link_dependencies(
  GoldenReport PRIVATE QT_IMAGE_FORMATS QT_OFFSCREEN QT_WINDOWS)
rellu_install_target(GoldenReport "${PROJECT_BINARY_DIR}/Tools/$<CONFIG>")
rellu_install_files("${PROJECT_BINARY_DIR}/Tools/$<CONFIG>/Fonts" ${FONT_FILES})
