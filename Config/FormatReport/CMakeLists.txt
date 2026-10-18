file(GLOB source_files ${RELLU_SOURCE_PATH}/FormatReport/*.cpp)
add_executable(FormatReport ${source_files})
target_link_libraries(FormatReport PRIVATE RelluFormat RelluIo)
rellu_install_target(FormatReport "${PROJECT_BINARY_DIR}/Tools/$<CONFIG>")
