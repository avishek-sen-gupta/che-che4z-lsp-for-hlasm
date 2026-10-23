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
#include <map>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include <nlohmann/json.hpp>

#include "../program_export.h"
#include "utils/path_conversions.h"

namespace {

struct bridge_run
{
    int exit_code;
    std::string out;
    std::string err;
    std::string source_uri;
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

bridge_run run_bridge_at(
    const std::filesystem::path& source_path, const std::string& source, const std::vector<std::string>& extra)
{
    write_file(source_path, source);
    std::vector<std::string> args { source_path.string() };
    args.insert(args.end(), extra.begin(), extra.end());
    std::ostringstream out;
    std::ostringstream err;
    const int code = hlasm_ast_bridge::run(args, out, err);
    const auto uri =
        hlasm_plugin::utils::path::path_to_uri(std::filesystem::absolute(source_path).lexically_normal().string());
    return { code, out.str(), err.str(), uri };
}

bridge_run run_bridge(const std::string& name, const std::string& source, const std::vector<std::string>& extra = { })
{ return run_bridge_at(scratch_dir(name) / "prog.asm", source, extra); }

nlohmann::json exported(const bridge_run& r)
{
    EXPECT_EQ(r.exit_code, 0) << r.err;
    EXPECT_EQ(r.err, "");
    return nlohmann::json::parse(r.out);
}

nlohmann::json open_code(const std::string& file, int line)
{ return { { "kind", "OPEN_CODE" }, { "file", file }, { "line", line } }; }

nlohmann::json in_macro(const std::string& file, int line, const std::string& macro, int depth)
{ return { { "kind", "MACRO" }, { "file", file }, { "line", line }, { "macro", macro }, { "depth", depth } }; }

nlohmann::json reg(int value) { return { { "kind", "REG" }, { "value", value } }; }
nlohmann::json mask(int value) { return { { "kind", "MASK" }, { "value", value } }; }
nlohmann::json storage(int displacement, int index, int base, nlohmann::json written)
{
    return {
        { "kind", "STORAGE" },
        { "displacement", displacement },
        { "index", index },
        { "base", base },
        { "written", std::move(written) },
    };
}
nlohmann::json storage_length(int displacement, int length, int base)
{ return { { "kind", "STORAGE_LENGTH" }, { "displacement", displacement }, { "length", length }, { "base", base } }; }

nlohmann::json machine(const std::string& section,
    int offset,
    const std::string& mnemonic,
    const std::string& base_mnemonic,
    const std::string& format,
    int length,
    nlohmann::json operands,
    nlohmann::json origin)
{
    return {
        { "kind", "MACHINE" },
        { "section", section },
        { "offset", offset },
        { "label", "" },
        { "mnemonic", mnemonic },
        { "base_mnemonic", base_mnemonic },
        { "format", format },
        { "length", length },
        { "operands", std::move(operands) },
        { "origin", std::move(origin) },
    };
}

nlohmann::json ds_item(const std::string& type, int duplication, int length)
{
    return {
        { "type", type },
        { "extension", "" },
        { "duplication", duplication },
        { "length", length },
        { "explicit_length", false },
    };
}

nlohmann::json dc_item(const std::string& type, int length, bool explicit_length, nlohmann::json nominal)
{
    auto item = ds_item(type, 1, length);
    item["explicit_length"] = explicit_length;
    item["nominal"] = std::move(nominal);
    return item;
}

nlohmann::json string_nominal(const std::string& value) { return { { "kind", "STRING" }, { "value", value } }; }

nlohmann::json addresses(nlohmann::json value)
{ return { { "kind", "ADDRESSES" }, { "values", { std::move(value) } } }; }

nlohmann::json reloc(const std::string& section, int offset)
{ return { { "kind", "RELOC" }, { "section", section }, { "offset", offset } }; }

nlohmann::json absolute(int value) { return { { "kind", "ABS" }, { "value", value } }; }

nlohmann::json reloc_symbol(
    const std::string& name, const std::string& section, int offset, const std::string& type, int length)
{
    return {
        { "kind", "RELOC" },
        { "name", name },
        { "section", section },
        { "offset", offset },
        { "type", type },
        { "length", length },
    };
}

nlohmann::json csect(const std::string& name, int length)
{ return { { "name", name }, { "kind", "CSECT" }, { "length", length } }; }

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

    nlohmann::json field_dc = {
        { "kind", "DC" },
        { "section", "HELLO" },
        { "offset", 8 },
        { "label", "FIELD" },
        { "mnemonic", "DC" },
        { "items", { dc_item("F", 4, false, string_nominal("7")) } },
        { "origin", open_code(r.source_uri, 5) },
    };
    const nlohmann::json expected = {
        { "format", 1 },
        { "sections", { csect("HELLO", 12) } },
        { "symbols", { reloc_symbol("FIELD", "HELLO", 8, "F", 4), reloc_symbol("HELLO", "HELLO", 0, "J", 1) } },
        { "entry", { { "kind", "NAMED" }, { "name", "HELLO" } } },
        { "externals", nlohmann::json::array() },
        { "statements",
            {
                machine("HELLO", 0, "LA", "LA", "RX-a", 4, { reg(15), storage(8, 0, 12, reloc("HELLO", 8)) }, open_code(r.source_uri, 3)),
                machine("HELLO", 4, "BR", "BCR", "RR", 2, { mask(15), reg(14) }, open_code(r.source_uri, 4)),
                field_dc,
            } },
        { "literals", nlohmann::json::array() },
        { "diagnostics", nlohmann::json::array() },
    };
    EXPECT_EQ(exported(r), expected);
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
        "         END\n");
    const auto program = exported(r);

    const nlohmann::json expected_dc = {
        { "kind", "DC" },
        { "section", "HELLO" },
        { "offset", 12 },
        { "label", "" },
        { "mnemonic", "DC" },
        { "items",
            {
                dc_item("C", 2, false, string_nominal("AB")),
                dc_item("X", 1, false, string_nominal("C1")),
                dc_item("F", 4, false, string_nominal("-1")),
                dc_item("P", 3, true, string_nominal("12")),
                dc_item("A", 4, false, addresses(reloc("HELLO", 8))),
                dc_item("V", 4, false, addresses({ { "kind", "EXTERNAL" }, { "name", "EXT" } })),
            } },
        { "origin", open_code(r.source_uri, 6) },
    };
    const nlohmann::json expected_ds = {
        { "kind", "DS" },
        { "section", "HELLO" },
        { "offset", 32 },
        { "label", "" },
        { "mnemonic", "DS" },
        { "items", { ds_item("H", 2, 2) } },
        { "origin", open_code(r.source_uri, 7) },
    };
    const auto& statements = program.at("statements");
    ASSERT_EQ(statements.size(), 5u);
    EXPECT_EQ(statements[3], expected_dc);
    EXPECT_EQ(statements[4], expected_ds);
    EXPECT_EQ(program.at("externals"), nlohmann::json { "EXT" });
    EXPECT_EQ(program.at("entry"), (nlohmann::json { { "kind", "DEFAULT" } }));
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

    const nlohmann::json expected = {
        machine("", 0, "LA", "LA", "RX-a", 4, { reg(1), storage(1, 0, 0, absolute(1)) }, in_macro(r.source_uri, 5, "M", 1)),
        machine("", 4, "BR", "BCR", "RR", 2, { mask(15), reg(14) }, in_macro(r.source_uri, 6, "RET", 1)),
    };
    EXPECT_EQ(exported(r).at("statements"), expected);
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
    EXPECT_EQ(diagnostics[0].at("file"), r.source_uri);
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

    const nlohmann::json expected_literals = {
        { { "section", "HELLO" }, { "offset", 16 }, { "item", dc_item("F", 4, false, string_nominal("5")) } },
        { { "section", "HELLO" }, { "offset", 20 }, { "item", dc_item("A", 4, false, addresses(reloc("HELLO", 4))) } },
    };
    EXPECT_EQ(program.at("literals"), expected_literals);
    const nlohmann::json expected_statements = {
        machine("HELLO", 0, "L", "L", "RX-a", 4, { reg(1), storage(16, 0, 12, reloc("HELLO", 16)) }, open_code(r.source_uri, 3)),
        machine("HELLO", 4, "L", "L", "RX-a", 4, { reg(2), storage(20, 0, 12, reloc("HELLO", 20)) }, open_code(r.source_uri, 4)),
        machine("HELLO", 8, "L", "L", "RX-a", 4, { reg(3), storage(16, 0, 12, reloc("HELLO", 16)) }, open_code(r.source_uri, 5)),
    };
    EXPECT_EQ(program.at("statements"), expected_statements);
    EXPECT_EQ(program.at("symbols"), (nlohmann::json { reloc_symbol("HELLO", "HELLO", 0, "J", 1) }));
    EXPECT_EQ(program.at("sections"), (nlohmann::json { csect("HELLO", 24) }));
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
    const nlohmann::json to_next = { mask(15), { { "kind", "RELOC_IMM" }, { "section", "HELLO" }, { "offset", 8 } } };
    EXPECT_EQ(statements[0], machine("HELLO", 0, "J", "BRC", "RI-c", 4, to_next, open_code(r.source_uri, 2)));
    const nlohmann::json counted = { mask(15), { { "kind", "IMM" }, { "value", 4 } } };
    EXPECT_EQ(statements[1], machine("HELLO", 4, "J", "BRC", "RI-c", 4, counted, open_code(r.source_uri, 3)));
}

TEST(program_export, exports_ss_operand_lengths)
{
    const auto r = run_bridge("ss",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         MVC   A(3),B\n"
        "         MVC   A,B\n"
        "A        DS    CL5\n"
        "B        DS    CL5\n"
        "         END\n");
    const auto program = exported(r);
    const auto& statements = program.at("statements");

    ASSERT_EQ(statements.size(), 4u);
    EXPECT_EQ(statements[0],
        machine("HELLO",
            0,
            "MVC",
            "MVC",
            "SS-a",
            6,
            { storage_length(12, 3, 12), storage(17, 0, 12, reloc("HELLO", 17)) },
            open_code(r.source_uri, 3)));
    EXPECT_EQ(statements[1],
        machine("HELLO",
            6,
            "MVC",
            "MVC",
            "SS-a",
            6,
            { storage_length(12, 5, 12), storage(17, 0, 12, reloc("HELLO", 17)) },
            open_code(r.source_uri, 4)));
}

TEST(program_export, exports_the_written_storage_address)
{
    const auto r = run_bridge("written",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         EX    2,MOVE\n"
        "         EX    2,MOVE(5)\n"
        "         EX    2,6(5,3)\n"
        "MOVE     MVC   0(0,3),0(4)\n"
        "         END\n");
    const auto program = exported(r);
    const auto& statements = program.at("statements");

    ASSERT_EQ(statements.size(), 4u);
    EXPECT_EQ(statements[0].at("operands"), (nlohmann::json { reg(2), storage(12, 0, 12, reloc("HELLO", 12)) }));
    EXPECT_EQ(statements[1].at("operands"), (nlohmann::json { reg(2), storage(12, 5, 12, reloc("HELLO", 12)) }));
    EXPECT_EQ(statements[2].at("operands"), (nlohmann::json { reg(2), storage(6, 5, 3, absolute(6)) }));
}

TEST(program_export, refuses_a_complex_relocatable_storage_address)
{
    const auto r = run_bridge("complex_written",
        "HELLO    CSECT\n"
        "         USING *,12\n"
        "         EX    2,MOVE+MOVE\n"
        "MOVE     MVC   0(0,3),0(4)\n"
        "         END\n");

    EXPECT_EQ(r.exit_code, 2);
    EXPECT_EQ(r.out, "");
    const auto diagnostics = nlohmann::json::parse(r.err).at("diagnostics");
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].at("code"), "ME009");
}

TEST(program_export, ignores_statements_after_end_and_reports_an_absolute_uri)
{
    const auto dir = scratch_dir("after_end");
    const auto relative = std::filesystem::relative(dir / "after_end.asm", std::filesystem::current_path());
    const auto r = run_bridge_at(relative,
        "HELLO    CSECT\n"
        "         BR    14\n"
        "         END   HELLO\n"
        "         LA    1,1\n",
        { });
    const auto program = exported(r);

    const auto absolute_uri =
        hlasm_plugin::utils::path::path_to_uri((std::filesystem::canonical(dir) / "after_end.asm").string());
    EXPECT_EQ(program.at("statements"),
        (nlohmann::json {
            machine("HELLO", 0, "BR", "BCR", "RR", 2, { mask(15), reg(14) }, open_code(absolute_uri, 2)) }));
    EXPECT_EQ(program.at("sections"), (nlohmann::json { csect("HELLO", 2) }));
}

TEST(program_export, names_the_open_code_source_verbatim)
{
    const auto ok = run_bridge("source_name",
        "         MACRO\n"
        "         M\n"
        "         LA    1,1\n"
        "         MEND\n"
        "         M\n"
        "         BR    14\n"
        "         END\n",
        { "--source-name", "src/hello.asm" });
    const auto bad = run_bridge("source_name_error", "         LA    99,0\n", { "--source-name", "src/hello.asm" });

    const nlohmann::json expected = {
        machine("", 0, "LA", "LA", "RX-a", 4, { reg(1), storage(1, 0, 0, absolute(1)) }, in_macro("src/hello.asm", 5, "M", 1)),
        machine("", 4, "BR", "BCR", "RR", 2, { mask(15), reg(14) }, open_code("src/hello.asm", 6)),
    };
    EXPECT_EQ(exported(ok).at("statements"), expected);
    EXPECT_EQ(bad.exit_code, 2);
    EXPECT_EQ(nlohmann::json::parse(bad.err).at("diagnostics")[0].at("file"), "src/hello.asm");
}

TEST(program_export, resolves_negated_load_store_on_condition_suffixes_to_the_principles_of_operation_masks)
{
    const std::vector<std::pair<std::string, std::string>> bases = {
        { "LOC", "1,0(12)" },
        { "LOCG", "1,0(12)" },
        { "LOCR", "1,2" },
        { "LOCGR", "1,2" },
        { "STOC", "1,0(12)" },
        { "STOCG", "1,0(12)" },
    };
    const std::vector<std::pair<std::string, int>> suffixes = {
        { "NE", 7 }, { "NZ", 7 }, { "NL", 11 }, { "NM", 11 }, { "NH", 13 }, { "NP", 13 },
    };
    std::string source = "T        CSECT\n";
    std::map<std::string, nlohmann::json> expected;
    for (const auto& [base, operands] : bases)
        for (const auto& [suffix, value] : suffixes)
        {
            source += "         " + base + suffix + " " + operands + "\n";
            expected[base + suffix] = { base, mask(value) };
        }
    source += "         END\n";

    const auto program = exported(run_bridge("negated_suffixes", source));
    std::map<std::string, nlohmann::json> resolved;
    for (const auto& s : program["statements"])
        resolved[s["mnemonic"].get<std::string>()] = { s["base_mnemonic"], s["operands"].back() };
    EXPECT_EQ(resolved, expected);
}

TEST(program_export, exports_vzero_as_vgbm_of_its_register_with_a_zero_mask)
{
    const auto program = exported(run_bridge("vzero",
        "T        CSECT\n"
        "         VZERO 5\n"
        "         VONE  20\n"
        "         END\n"));
    const nlohmann::json vector5 = { { "kind", "VEC_REG" }, { "value", 5 } };
    const nlohmann::json vector20 = { { "kind", "VEC_REG" }, { "value", 20 } };
    const nlohmann::json zero = { { "kind", "IMM" }, { "value", 0 } };
    const nlohmann::json ones = { { "kind", "IMM" }, { "value", 65535 } };
    std::vector<nlohmann::json> resolved;
    for (const auto& s : program["statements"])
        resolved.push_back({ s["mnemonic"], s["base_mnemonic"], s["operands"] });
    const std::vector<nlohmann::json> expected = {
        { "VZERO", "VGBM", { vector5, zero } },
        { "VONE", "VGBM", { vector20, ones } },
    };
    EXPECT_EQ(resolved, expected);
}
