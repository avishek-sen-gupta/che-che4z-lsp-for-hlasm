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

project(hlasm_ast_bridge)

add_executable(hlasm_ast_bridge
    main.cpp
    instruction_table_export.h
    program_export.h
    statement_collector.h
)

target_compile_features(hlasm_ast_bridge PRIVATE cxx_std_20)
target_compile_options(hlasm_ast_bridge PRIVATE ${HLASM_EXTRA_FLAGS})
set_target_properties(hlasm_ast_bridge PROPERTIES CXX_EXTENSIONS OFF)

target_include_directories(hlasm_ast_bridge
    PRIVATE
    ../parser_library/src
    ../hlasm_export
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(hlasm_ast_bridge PRIVATE parser_library hlasm_utils)
target_link_libraries(hlasm_ast_bridge PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(hlasm_ast_bridge PRIVATE Threads::Threads)

target_link_options(hlasm_ast_bridge PRIVATE ${HLASM_EXTRA_LINKER_FLAGS})

if(BUILD_TESTING)
    add_executable(hlasm_ast_bridge_test
        test/instruction_table_test.cpp
        test/program_export_test.cpp
    )

    target_compile_features(hlasm_ast_bridge_test PRIVATE cxx_std_20)
    target_compile_options(hlasm_ast_bridge_test PRIVATE ${HLASM_EXTRA_FLAGS})
    set_target_properties(hlasm_ast_bridge_test PROPERTIES CXX_EXTENSIONS OFF)

    target_include_directories(hlasm_ast_bridge_test
        PRIVATE
        ../parser_library/src
        ../hlasm_export
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    target_link_libraries(hlasm_ast_bridge_test PRIVATE parser_library hlasm_utils)
    target_link_libraries(hlasm_ast_bridge_test PRIVATE gmock_main)
    target_link_libraries(hlasm_ast_bridge_test PRIVATE nlohmann_json::nlohmann_json)

    target_link_options(hlasm_ast_bridge_test PRIVATE ${HLASM_EXTRA_LINKER_FLAGS})

    if(DISCOVER_TESTS)
        gtest_discover_tests(hlasm_ast_bridge_test DISCOVERY_TIMEOUT 120)
    endif()
endif()
