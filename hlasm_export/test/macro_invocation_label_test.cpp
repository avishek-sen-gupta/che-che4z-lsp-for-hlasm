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
#include "compiler_options.h"
#include "../macro_invocation_analyzer.h"

// The name field of a macro invocation is not required to be an ordinary
// symbol. HLASM models that with two label kinds -- ORD for a valid ordinary
// symbol, MAC for any other name-field text -- and a caller that reads only ORD
// silently loses the name.
//
// BMS is the case that matters here: DFHMDF field names follow the rules of the
// generated language, so under LANG=COBOL a field may legitimately be called
// SC-ELIG. That is not an ordinary symbol (HLASM symbols admit no hyphen), so it
// arrives as MAC. Dropping it makes the invocation indistinguishable from a
// genuinely unnamed constant field, and every symbolic name BMS derives from it
// (SC-ELIGL, SC-ELIGA, SC-ELIGO, ...) vanishes from the generated copybook with
// no error anywhere.

namespace {

// Minimal DFHMDF prototype: enough to make the invocation a macro call, with no
// body, so nothing here depends on the name field being usable as a symbol.
const std::string dfhmdf_src = " MACRO\n"
                               "&NAME    DFHMDF &POS=,&LENGTH=,&ATTRB=\n"
                               "         MEND\n";

std::vector<hlasm_plugin::parser_library::macro_invocation_record> invocations_of(
    const std::string& source)
{
    using namespace hlasm_plugin::parser_library;

    mock_parse_lib_provider libs { { "DFHMDF", dfhmdf_src } };
    macro_invocation_analyzer inv_analyzer;
    analyzer a(source, analyzer_options { &libs, file_is_opencode::yes });
    a.register_stmt_analyzer(&inv_analyzer);
    a.analyze();
    return inv_analyzer.invocations;
}

} // namespace

TEST(macro_invocation_label, hyphenated_name_field_is_captured)
{
    auto invocations = invocations_of("PLAIN    DFHMDF POS=(1,1),LENGTH=4\n"
                                     "SC-ELIG  DFHMDF POS=(2,1),LENGTH=1\n"
                                     "         DFHMDF POS=(3,1),LENGTH=2\n");

    ASSERT_EQ(invocations.size(), 3U);
    EXPECT_EQ(invocations[0].label, "PLAIN") << "an ordinary symbol still comes through";
    EXPECT_EQ(invocations[1].label, "SC-ELIG") << "a hyphenated BMS field name is a name, not noise";
    EXPECT_EQ(invocations[2].label, "") << "a genuinely unnamed field stays unnamed";
}
