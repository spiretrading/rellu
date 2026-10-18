file(GLOB source_files ${RELLU_SOURCE_PATH}/XdReport/*.cpp)
add_executable(XdReport ${source_files})
target_link_libraries(XdReport PRIVATE RelluIo)
rellu_install_target(XdReport "${PROJECT_BINARY_DIR}/Tools/$<CONFIG>")
