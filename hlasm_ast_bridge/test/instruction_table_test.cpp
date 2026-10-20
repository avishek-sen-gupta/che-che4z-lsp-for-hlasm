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

// The instruction-table export: one JSON row per machine instruction and per extended mnemonic.

#include <algorithm>
#include <sstream>
#include <string>

#include "gtest/gtest.h"

#include <nlohmann/json.hpp>

#include "../instruction_table_export.h"
#include "instructions/instruction.h"

namespace {

nlohmann::json exported_table()
{
    std::ostringstream out;
    hlasm_ast_bridge::export_instruction_table(out);
    return nlohmann::json::parse(out.str());
}

nlohmann::json row_named(const nlohmann::json& table, const std::string& mnemonic)
{
    const auto& rows = table.at("instructions");
    const auto it = std::ranges::find_if(rows, [&](const nlohmann::json& r) { return r.at("mnemonic") == mnemonic; });
    return it == rows.end() ? nlohmann::json() : *it;
}

const nlohmann::json none_param = { { "kind", "NONE" }, { "bits", 0 }, { "signed", false } };

} // namespace

TEST(instruction_table, exports_lr_lpsw_be_and_every_row)
{
    using namespace hlasm_plugin::parser_library::instructions;
    const auto table = exported_table();

    EXPECT_EQ(table.at("format"), 1);
    EXPECT_EQ(table.at("instructions").size(), all_machine_instructions().size() + all_mnemonic_codes().size());
    EXPECT_EQ(table.at("instructions").size(), 1395u + 1120u);

    const nlohmann::json reg_operand = {
        { "kind", "REG" },
        { "bits", 4 },
        { "signed", false },
        { "first", none_param },
        { "second", none_param },
        { "optional", false },
    };
    const nlohmann::json expected_lr = {
        { "mnemonic", "LR" },
        { "format", "RR" },
        { "privileged", "not_privileged" },
        { "arch", "UNI_ESA_XA_370_DOS_SINCE_ZOP" },
        { "operands", { reg_operand, reg_operand } },
        { "base", nullptr },
        { "transforms", nlohmann::json::array() },
    };
    EXPECT_EQ(row_named(table, "LR"), expected_lr);

    const auto lpsw = row_named(table, "LPSW");
    EXPECT_EQ(lpsw.at("privileged"), "privileged");
    EXPECT_EQ(lpsw.at("format"), "SI");
    EXPECT_EQ(lpsw.at("base"), nullptr);

    const auto be = row_named(table, "BE");
    EXPECT_EQ(be.at("base"), "BC");
    EXPECT_EQ(be.at("format"), "RX-b");
    EXPECT_EQ(be.at("arch"), "UNI_ESA_XA_370_DOS_SINCE_ZOP");
    const nlohmann::json expected_be_transforms = { {
        { "kind", "value" },
        { "skip", 0 },
        { "value", 8 },
        { "source", 0 },
        { "insert", true },
    } };
    EXPECT_EQ(be.at("transforms"), expected_be_transforms);
}
