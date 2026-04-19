cmake_minimum_required(VERSION 3.16)

set(source_dir "${CMAKE_CURRENT_LIST_DIR}/..")
set(build_root "${CMAKE_CURRENT_BINARY_DIR}/issue314_build_graph")
file(REMOVE_RECURSE "${build_root}")

function(issue314_configure name)
    set(build_dir "${build_root}/${name}")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}"
            -S "${source_dir}"
            -B "${build_dir}"
            -DBUILD_TESTING=${ARGN}
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Configure failed for ${name}:\n${output}\n${error}")
    endif()

    set("${name}_build_dir" "${build_dir}" PARENT_SCOPE)
    set("${name}_output" "${output}\n${error}" PARENT_SCOPE)
endfunction()

function(issue314_assert_target build_dir target should_exist)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" --build "${build_dir}" --target help
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
    )
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Target listing failed for ${build_dir}:\n${output}\n${error}")
    endif()

    string(REGEX MATCH "(^|\n)\\.\\.\\. ${target}($|\n)" found "${output}")
    if(should_exist AND NOT found)
        message(FATAL_ERROR "Expected target '${target}' in ${build_dir}, but it was absent")
    endif()
    if(NOT should_exist AND found)
        message(FATAL_ERROR "Target '${target}' was unexpectedly present in ${build_dir}")
    endif()
endfunction()

issue314_configure(no_tests OFF -DPMM_BUILD_EXAMPLES=OFF)
if(EXISTS "${no_tests_build_dir}/_deps/catch2-src")
    message(FATAL_ERROR "Catch2 was fetched even though BUILD_TESTING=OFF")
endif()
issue314_assert_target("${no_tests_build_dir}" basic_usage FALSE)

issue314_configure(compact_tests ON -DPMM_BUILD_EXAMPLES=OFF)
issue314_assert_target("${compact_tests_build_dir}" test_allocate TRUE)
issue314_assert_target("${compact_tests_build_dir}" basic_usage FALSE)

issue314_configure(default_graph ON -DPMM_BUILD_EXAMPLES=ON)
issue314_assert_target("${default_graph_build_dir}" test_allocate TRUE)
issue314_assert_target("${default_graph_build_dir}" basic_usage TRUE)
