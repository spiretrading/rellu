file(GLOB source_files ${RELLU_SOURCE_PATH}/PrototypeReport/*.cpp)
add_executable(PrototypeReport ${source_files})
target_link_libraries(PrototypeReport PRIVATE RelluPrototype)
rellu_install_target(PrototypeReport "${PROJECT_BINARY_DIR}/Tools/$<CONFIG>")
