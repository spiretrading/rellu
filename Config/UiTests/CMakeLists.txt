file(GLOB source_files ${RELLU_SOURCE_PATH}/UiTests/*.cpp
  ${RELLU_SOURCE_PATH}/UiTests/*.hpp)
add_executable(UiTests ${source_files})
target_link_libraries(UiTests PRIVATE RelluUi)
rellu_link_dependencies(UiTests PRIVATE DOCTEST QT_OFFSCREEN)
add_custom_command(TARGET UiTests POST_BUILD COMMAND UiTests)
rellu_install_target(UiTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
