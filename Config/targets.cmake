function(rellu_track_target target)
  set_property(GLOBAL APPEND PROPERTY RELLU_BUILD_TARGETS ${target})
endfunction()

function(rellu_install_target target directory)
  install(TARGETS ${target} DESTINATION "${directory}")
  rellu_track_target(${target})
  set_property(GLOBAL APPEND PROPERTY RELLU_INSTALLED_OUTPUTS
    "${directory}/$<TARGET_FILE_NAME:${target}>")
endfunction()

function(rellu_add_modules directory)
  file(GLOB modules LIST_DIRECTORIES true "${directory}/*")
  list(SORT modules)
  foreach(module IN LISTS modules)
    if(EXISTS "${module}/CMakeLists.txt")
      add_subdirectory("${module}")
    endif()
  endforeach()
endfunction()

function(rellu_configure_clean)
  get_property(targets GLOBAL PROPERTY RELLU_BUILD_TARGETS)
  get_property(clean_outputs GLOBAL PROPERTY RELLU_INSTALLED_OUTPUTS)
  foreach(target IN LISTS targets)
    get_target_property(type ${target} TYPE)
    list(APPEND clean_outputs
      "$<TARGET_FILE:${target}>" "$<TARGET_OBJECTS:${target}>")
    if(MSVC)
      get_target_property(target_directory ${target} BINARY_DIR)
      set(compile_directory "${target_directory}/${target}.dir/$<CONFIG>")
      set_target_properties(${target} PROPERTIES COMPILE_PDB_NAME "${target}"
        COMPILE_PDB_OUTPUT_DIRECTORY "${compile_directory}")
      list(APPEND clean_outputs "${compile_directory}/${target}.pdb")
      if(NOT type STREQUAL "STATIC_LIBRARY")
        list(APPEND clean_outputs "$<TARGET_PDB_FILE:${target}>"
          "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_BASE_NAME:${target}>.ilk")
      endif()
    endif()
  endforeach()
  configure_file("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/clean.cmake"
    CMakeFiles/clean.cmake.in @ONLY)
  file(GENERATE
    OUTPUT
      "${PROJECT_BINARY_DIR}/CMakeFiles/rellu_clean_outputs_$<CONFIG>.cmake"
    INPUT "${PROJECT_BINARY_DIR}/CMakeFiles/clean.cmake.in")
endfunction()
