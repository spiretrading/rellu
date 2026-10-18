function(rellu_track_target target)
  set_property(GLOBAL APPEND PROPERTY RELLU_BUILD_TARGETS ${target})
endfunction()

function(rellu_install_target target directory)
  install(TARGETS ${target} DESTINATION "${directory}")
  rellu_track_target(${target})
  set_property(GLOBAL APPEND PROPERTY RELLU_INSTALLED_OUTPUTS
    "${directory}/$<TARGET_FILE_NAME:${target}>")
endfunction()

function(rellu_install_files directory)
  install(FILES ${ARGN} DESTINATION "${directory}")
  foreach(file IN LISTS ARGN)
    cmake_path(GET file FILENAME name)
    set_property(GLOBAL APPEND PROPERTY RELLU_INSTALLED_OUTPUTS
      "${directory}/${name}")
  endforeach()
endfunction()

function(rellu_copy_files target directory)
  set(outputs)
  foreach(file IN LISTS ARGN)
    cmake_path(GET file FILENAME name)
    set(output "${directory}/${name}")
    add_custom_command(OUTPUT "${output}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${file}" "${output}"
      DEPENDS "${file}" VERBATIM)
    list(APPEND outputs "${output}")
  endforeach()
  add_custom_target(${target} ALL DEPENDS ${outputs})
  set_property(GLOBAL APPEND PROPERTY RELLU_INSTALLED_OUTPUTS ${outputs})
endfunction()

function(rellu_link_dependencies target scope)
  foreach(dependency IN LISTS ARGN)
    if(DEFINED ${dependency}_INCLUDE_PATH)
      target_include_directories(
        ${target} SYSTEM ${scope} ${${dependency}_INCLUDE_PATH})
    endif()
    if(DEFINED ${dependency}_COMPILE_DEFINITIONS)
      target_compile_definitions(
        ${target} ${scope} ${${dependency}_COMPILE_DEFINITIONS})
    endif()
    if(DEFINED ${dependency}_COMPILE_OPTIONS)
      target_compile_options(
        ${target} ${scope} ${${dependency}_COMPILE_OPTIONS})
    endif()
    if(DEFINED ${dependency}_LIBRARY_DEBUG_PATH OR
        DEFINED ${dependency}_LIBRARY_OPTIMIZED_PATH)
      foreach(debug optimized IN ZIP_LISTS ${dependency}_LIBRARY_DEBUG_PATH
          ${dependency}_LIBRARY_OPTIMIZED_PATH)
        if(NOT debug OR NOT optimized)
          message(FATAL_ERROR "Incomplete library paths for ${dependency}.")
        endif()
        target_link_libraries(
          ${target} ${scope} debug ${debug} optimized ${optimized})
      endforeach()
    endif()
    if(DEFINED ${dependency}_LIBRARIES)
      target_link_libraries(${target} ${scope} ${${dependency}_LIBRARIES})
    endif()
  endforeach()
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
