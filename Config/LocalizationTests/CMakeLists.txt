file(GLOB_RECURSE source_files ${RELLU_SOURCE_PATH}/LocalizationTests/*.cpp)
add_executable(LocalizationTests ${source_files})
target_include_directories(
  LocalizationTests SYSTEM PRIVATE ${DOCTEST_INCLUDE_PATH})
target_link_libraries(LocalizationTests PRIVATE RelluLocalization)
add_custom_command(
  TARGET LocalizationTests POST_BUILD COMMAND LocalizationTests)
rellu_install_target(LocalizationTests "${TEST_INSTALL_DIRECTORY}/$<CONFIG>")
