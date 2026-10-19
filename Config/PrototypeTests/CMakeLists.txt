file(GLOB source_files ${RELLU_SOURCE_PATH}/PrototypeTests/*.cpp
  ${RELLU_SOURCE_PATH}/PrototypeTests/*.hpp)
add_executable(PrototypeTests ${source_files})
target_link_libraries(PrototypeTests PRIVATE RelluPrototype)
rellu_link_dependencies(PrototypeTests PRIVATE DOCTEST QT_OFFSCREEN)
add_custom_command(TARGET PrototypeTests POST_BUILD COMMAND PrototypeTests)
rellu_install_target(PrototypeTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
