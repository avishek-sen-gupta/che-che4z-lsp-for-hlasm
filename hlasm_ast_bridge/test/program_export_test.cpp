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

// The statement-stream export: resolved statements, symbols, sections and literals of one assembly.

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

#include "../program_export.h"

namespace {

struct bridge_run
{
    int exit_code;
    std::string out;
    std::string err;
    std::string source_path;
};

std::filesystem::path scratch_dir(const std::string& name)
{
    auto dir = std::filesystem::temp_directory_path() / ("hlasm_ast_bridge_test_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(const std::filesystem::path& path, const std::string& text)
{ std::ofstream(path, std::ios::binary) << text; }

bridge_run run_bridge(const std::string& name, const std::string& source, const std::vector<std::string>& extra = { })
{
    const auto dir = scratch_dir(name);
    const auto source_path = (dir / "prog.asm").string();
    write_file(source_path, source);
    std::vector<std::string> args { source_path };
    args.insert(args.end(), extra.begin(), extra.end());
    std::ostringstream out;
    std::ostringstream err;
    const int code = hlasm_ast_bridge::run(args, out, err);
    return { code, out.str(), err.str(), source_path };
}

nlohmann::json exported(const bridge_run& r)
{
    EXPECT_EQ(r.exit_code, 0) << r.err;
    EXPECT_EQ(r.err, "");
    return nlohmann::json::parse(r.out);
}

nlohmann::json origin(const std::string& file, int line, nlohmann::json macro, int depth)
{ return { { "file", file }, { "line", line }, { "macro", std::move(macro) }, { "depth", depth } }; }

nlohmann::json reg(int value) { return { { "kind", "REG" }, { "value", value } }; }
nlohmann::json mask(int value) { return { { "kind", "MASK" }, { "value", value } }; }
nlohmann::json storage(int displacement, int index, int base)
{
    return {
        { "kind", "STORAGE" },
        { "displacement", displacement },
        { "index", index },
        { "base", base },
        { "length", nullptr },
    };
}

nlohmann::json data_item(const std::string& type,
    int length,
    bool explicit_length,
    nlohmann::json nominal,
    const std::string& extension = "")
{
    return {
        { "type", type },
        { "extension", extension },
        { "duplication", 1 },
        { "length", length },
        { "explicit_length", explicit_length },
        { "scale", 0 },
        { "exponent", 0 },
        { "nominal", std::move(nominal) },
    };
}

nlohmann::json string_nominal(const std::string& value) { return { { "kind", "STRING" }, { "value", value } }; }

} // namespace

TEST(program_export, exports_resolved_machine_statement)
{
    const auto r = run_bridge("machine",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         LA    15,FIELD\n"
        "         BR    14\n"
        "FIELD    DC    F'7'\n"
        "         END   HELLO\n");
    const auto program = exported(r);

    EXPECT_EQ(program.at("format"), 1);
    const nlohmann::json expected_sections = { { { "name", "HELLO" }, { "kind", "CSECT" }, { "length", 12 } } };
    EXPECT_EQ(program.at("sections"), expected_sections);
    EXPECT_EQ(program.at("entry"), "HELLO");
    EXPECT_EQ(program.at("externals"), nlohmann::json::array());
    EXPECT_EQ(program.at("literals"), nlohmann::json::array());
    EXPECT_EQ(program.at("diagnostics"), nlohmann::json::array());

    const auto& statements = program.at("statements");
    ASSERT_EQ(statements.size(), 3u);
    const nlohmann::json expected_la = {
        { "section", "HELLO" },
        { "offset", 0 },
        { "label", nullptr },
        { "mnemonic", "LA" },
        { "base_mnemonic", "LA" },
        { "format", "RX-a" },
        { "length", 4 },
        { "operands", { reg(15), storage(8, 0, 12) } },
        { "data", nlohmann::json::array() },
        { "origin", origin(r.source_path, 3, nullptr, 0) },
    };
    EXPECT_EQ(statements[0], expected_la);
    const nlohmann::json expected_br = {
        { "section", "HELLO" },
        { "offset", 4 },
        { "label", nullptr },
        { "mnemonic", "BR" },
        { "base_mnemonic", "BCR" },
        { "format", "RR" },
        { "length", 2 },
        { "operands", { mask(15), reg(14) } },
        { "data", nlohmann::json::array() },
        { "origin", origin(r.source_path, 4, nullptr, 0) },
    };
    EXPECT_EQ(statements[1], expected_br);
    EXPECT_EQ(statements[2].at("offset"), 8);
    EXPECT_EQ(statements[2].at("label"), "FIELD");

    const nlohmann::json expected_field = {
        { "name", "FIELD" },
        { "kind", "RELOC" },
        { "section", "HELLO" },
        { "offset", 8 },
        { "value", nullptr },
        { "length", 4 },
        { "type", "F" },
        { "scale", 0 },
        { "integer", 31 },
    };
    const auto& symbols = program.at("symbols");
    const auto field = std::ranges::find_if(symbols, [](const nlohmann::json& s) { return s.at("name") == "FIELD"; });
    ASSERT_NE(field, symbols.end());
    EXPECT_EQ(*field, expected_field);
}

TEST(program_export, exports_dc_nominals)
{
    const auto r = run_bridge("dc",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         LA    15,FIELD\n"
        "         BR    14\n"
        "FIELD    DC    F'7'\n"
        "         DC    C'AB',X'C1',F'-1',PL3'12',A(FIELD),V(EXT)\n"
        "         DS    2H\n"
        "         END   HELLO\n");
    const auto program = exported(r);

    const auto& statements = program.at("statements");
    ASSERT_EQ(statements.size(), 5u);
    const nlohmann::json expected_data = {
        data_item("C", 2, false, string_nominal("AB")),
        data_item("X", 1, false, string_nominal("C1")),
        data_item("F", 4, false, string_nominal("-1")),
        data_item("P", 3, true, string_nominal("12")),
        data_item("A",
            4,
            false,
            { { "kind", "ADDRESSES" },
                { "values", { { { "kind", "RELOC" }, { "section", "HELLO" }, { "offset", 8 } } } } }),
        data_item("V",
            4,
            false,
            { { "kind", "ADDRESSES" }, { "values", { { { "kind", "EXTERNAL" }, { "name", "EXT" } } } } }),
    };
    const nlohmann::json expected_dc = {
        { "section", "HELLO" },
        { "offset", 12 },
        { "label", nullptr },
        { "mnemonic", "DC" },
        { "operands", nlohmann::json::array() },
        { "data", expected_data },
        { "origin", origin(r.source_path, 6, nullptr, 0) },
    };
    EXPECT_EQ(statements[3], expected_dc);

    nlohmann::json ds_item = data_item("H", 2, false, nullptr);
    ds_item["duplication"] = 2;
    const nlohmann::json expected_ds = {
        { "section", "HELLO" },
        { "offset", 32 },
        { "label", nullptr },
        { "mnemonic", "DS" },
        { "operands", nlohmann::json::array() },
        { "data", { ds_item } },
        { "origin", origin(r.source_path, 7, nullptr, 0) },
    };
    EXPECT_EQ(statements[4], expected_ds);
    EXPECT_EQ(program.at("externals"), nlohmann::json { "EXT" });
}

TEST(program_export, exports_macro_origin)
{
    const auto dir = scratch_dir("macro_syslib");
    write_file(dir / "RET",
        "         MACRO\n"
        "         RET\n"
        "         BR    14\n"
        "         MEND\n");
    const auto r = run_bridge("macro",
        "         MACRO\n"
        "         M\n"
        "         LA    1,1\n"
        "         MEND\n"
        "         M\n"
        "         RET\n"
        "         END\n",
        { "--syslib", dir.string() });
    const auto program = exported(r);

    const auto& statements = program.at("statements");
    ASSERT_EQ(statements.size(), 2u);
    EXPECT_EQ(statements[0].at("mnemonic"), "LA");
    EXPECT_EQ(statements[0].at("section"), "");
    EXPECT_EQ(statements[0].at("offset"), 0);
    EXPECT_EQ(statements[0].at("origin"), origin(r.source_path, 5, "M", 1));
    EXPECT_EQ(statements[1].at("mnemonic"), "BR");
    EXPECT_EQ(statements[1].at("offset"), 4);
    EXPECT_EQ(statements[1].at("origin"), origin(r.source_path, 6, "RET", 1));
}

TEST(program_export, refuses_assembly_errors)
{
    const auto r = run_bridge("errors",
        "         LA    99,0\n"
        "         END\n");

    EXPECT_EQ(r.exit_code, 2);
    EXPECT_EQ(r.out, "");
    const auto diagnostics = nlohmann::json::parse(r.err).at("diagnostics");
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].at("code"), "M120");
    EXPECT_EQ(diagnostics[0].at("severity"), "ERROR");
    EXPECT_EQ(diagnostics[0].at("file"), r.source_path);
    EXPECT_EQ(diagnostics[0].at("line"), 1);
}

TEST(program_export, exports_literal_pool)
{
    const auto r = run_bridge("literal",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         L     1,=F'5'\n"
        "         L     2,=A(*)\n"
        "         L     3,=F'5'\n"
        "         END\n");
    const auto program = exported(r);

    const nlohmann::json here = {
        { "kind", "ADDRESSES" },
        { "values", { { { "kind", "RELOC" }, { "section", "HELLO" }, { "offset", 4 } } } },
    };
    const nlohmann::json expected_literals = {
        { { "section", "HELLO" }, { "offset", 16 }, { "data", { data_item("F", 4, false, string_nominal("5")) } } },
        { { "section", "HELLO" }, { "offset", 20 }, { "data", { data_item("A", 4, false, here) } } },
    };
    EXPECT_EQ(program.at("literals"), expected_literals);
    const auto& statements = program.at("statements");
    EXPECT_EQ(statements.at(0).at("operands"), (nlohmann::json { reg(1), storage(16, 0, 12) }));
    EXPECT_EQ(statements.at(1).at("operands"), (nlohmann::json { reg(2), storage(20, 0, 12) }));
    EXPECT_EQ(statements.at(2).at("operands"), (nlohmann::json { reg(3), storage(16, 0, 12) }));
    const auto& symbols = program.at("symbols");
    EXPECT_EQ(symbols.size(), 1u);
    EXPECT_EQ(symbols.at(0).at("name"), "HELLO");
    EXPECT_EQ(program.at("sections").at(0).at("length"), 24);
}

TEST(program_export, exports_relative_branch_targets)
{
    const auto r = run_bridge("relative",
        "HELLO    CSECT\n"
        "         J     NEXT\n"
        "         J     4\n"
        "NEXT     BR    14\n"
        "         END\n");
    const auto program = exported(r);

    const auto& statements = program.at("statements");
    ASSERT_EQ(statements.size(), 3u);
    const nlohmann::json to_next = {
        mask(15),
        { { "kind", "RELOC_IMM" }, { "section", "HELLO" }, { "offset", 8 } },
    };
    EXPECT_EQ(statements[0].at("base_mnemonic"), "BRC");
    EXPECT_EQ(statements[0].at("operands"), to_next);
    const nlohmann::json counted = { mask(15), { { "kind", "IMM" }, { "value", 4 } } };
    EXPECT_EQ(statements[1].at("operands"), counted);
}
