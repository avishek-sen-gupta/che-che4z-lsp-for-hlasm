# Copyright (c) 2019 Broadcom.
# The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
#
# This program and the accompanying materials are made
# available under the terms of the Eclipse Public License 2.0
# which is available at https://www.eclipse.org/legal/epl-2.0/
#
# SPDX-License-Identifier: EPL-2.0
#
# Contributors:
#   Broadcom, Inc. - initial API and implementation

project(hlasm_export)

# --- CLI executable ---

add_executable(hlasm_export
    main.cpp
    hlasm_json_export.h
)

target_compile_features(hlasm_export PRIVATE cxx_std_20)
target_compile_options(hlasm_export PRIVATE ${HLASM_EXTRA_FLAGS})
set_target_properties(hlasm_export PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(hlasm_export
    PRIVATE
    ../parser_library/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hlasm_export PRIVATE parser_library hlasm_utils)
target_link_libraries(hlasm_export PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(hlasm_export PRIVATE Threads::Threads)

target_link_options(hlasm_export PRIVATE ${HLASM_EXTRA_LINKER_FLAGS})

# --- Test binary ---

if(BUILD_TESTING)
    add_executable(hlasm_export_test
        ../parser_library/test/common_testing.cpp
        ../parser_library/test/gtest_stringers.cpp
        test/experiment_hlasm_test.cpp
        test/experiment_bms_test.cpp
        test/experiment_json_export_test.cpp
    )

    target_compile_features(hlasm_export_test PRIVATE cxx_std_20)
    target_compile_options(hlasm_export_test PRIVATE ${HLASM_EXTRA_FLAGS})
    set_target_properties(hlasm_export_test PROPERTIES CXX_EXTENSIONS OFF)

    target_include_directories(hlasm_export_test
        PRIVATE
        ../parser_library/src
        ../parser_library/test
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(hlasm_export_test PRIVATE parser_library hlasm_utils)
    target_link_libraries(hlasm_export_test PRIVATE gmock_main)
    target_link_libraries(hlasm_export_test PRIVATE nlohmann_json::nlohmann_json)

    target_link_options(hlasm_export_test PRIVATE ${HLASM_EXTRA_LINKER_FLAGS})

    if(DISCOVER_TESTS)
        gtest_discover_tests(hlasm_export_test DISCOVERY_TIMEOUT 120)
    endif()
endif()
