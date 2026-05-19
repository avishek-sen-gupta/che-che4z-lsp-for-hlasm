/*
 * Copyright (c) 2019 Broadcom.
 * The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
 *
 * This program and the accompanying materials are made
 * available under the terms of the Eclipse Public License 2.0
 * which is available at https://www.eclipse.org/legal/epl-2.0/
 *
 * SPDX-License-Identifier: EPL-2.0
 *
 * Contributors:
 *   Broadcom, Inc. - initial API and implementation
 */

#include "common_testing.h"
#include "mock_parse_lib_provider.h"
#include "context/hlasm_context.h"
#include "context/ordinary_assembly/ordinary_assembly_context.h"
#include "context/ordinary_assembly/section.h"

// Experiment 1: Basic HLASM source with a COPY member (register EQUs)
// and a simple macro (HELLO). Verifies symbol table contents post-analysis.

TEST(experiment_hlasm, copy_equ_and_macro)
{
    // COPY member: defines R0-R15 as EQU constants
    static const std::string equregs_src = R"(
R0       EQU   0
R1       EQU   1
R2       EQU   2
R3       EQU   3
R4       EQU   4
R5       EQU   5
R6       EQU   6
R7       EQU   7
R8       EQU   8
R9       EQU   9
R10      EQU   10
R11      EQU   11
R12      EQU   12
R13      EQU   13
R14      EQU   14
R15      EQU   15
)";

    // Macro: takes a register number, emits LR reg,0
    static const std::string hello_src = R"( MACRO
         HELLO &REG
         LR    &REG,0
         MEND
)";

    mock_parse_lib_provider libs {
        { "EQUREGS", equregs_src },
        { "HELLO", hello_src },
    };

    std::string input = R"(
MYPROG   CSECT
         COPY  EQUREGS
         HELLO 1
FOO      EQU   42
         END
)";

    analyzer a(input, analyzer_options { &libs });
    a.analyze();

    EXPECT_TRUE(a.diags().empty());

    auto& ctx = a.hlasm_ctx();

    // CSECT was created
    EXPECT_NE(get_symbol(ctx, "MYPROG"), nullptr);
    const auto* myprog_sec = get_section(ctx, "MYPROG");
    ASSERT_NE(myprog_sec, nullptr);
    EXPECT_EQ(myprog_sec->kind, context::section_kind::EXECUTABLE);

    // COPY member was processed: R0..R15 defined as absolute EQUs
    for (int i = 0; i <= 15; ++i)
    {
        auto val = get_symbol_abs(ctx, "R" + std::to_string(i));
        ASSERT_TRUE(val.has_value()) << "R" << i << " not defined";
        EXPECT_EQ(*val, i) << "R" << i << " has wrong value";
    }

    // Literal EQU in open code
    auto foo = get_symbol_abs(ctx, "FOO");
    ASSERT_TRUE(foo.has_value());
    EXPECT_EQ(*foo, 42);

    // Macro was parsed (it's in HELLO library, not open-code)
    // The library provider parses macros on demand, so we only assert
    // the main context saw no diagnostic from macro invocation.
    EXPECT_EQ(a.hlasm_ctx().copy_members().size(), 1U);
}
