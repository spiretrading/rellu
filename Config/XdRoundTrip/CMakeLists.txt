file(GLOB source_files ${RELLU_SOURCE_PATH}/XdRoundTrip/*.cpp)
add_executable(XdRoundTrip ${source_files})
target_link_libraries(XdRoundTrip PRIVATE RelluIo)
rellu_install_target(XdRoundTrip "${PROJECT_BINARY_DIR}/Tools/$<CONFIG>")
