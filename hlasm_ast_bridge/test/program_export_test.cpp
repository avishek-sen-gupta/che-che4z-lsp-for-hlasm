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

nlohmann::json ds_item(const std::string& type, int duplication, int length, int offset)
{
    return {
        { "type", type },
        { "extension", "" },
        { "duplication", duplication },
        { "length", length },
        { "length_unit", "BYTE" },
        { "explicit_length", false },
        { "scale", 0 },
        { "exponent", 0 },
        { "offset", offset },
        { "bit_offset", 0 },
    };
}

nlohmann::json dc_item(const std::string& type, int length, bool explicit_length, nlohmann::json nominal, int offset)
{
    auto item = ds_item(type, 1, length, offset);
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
        { "items", { dc_item("F", 4, false, string_nominal("7"), 8) } },
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
        { "amodes", nlohmann::json::array() },
        { "rmodes", nlohmann::json::array() },
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
                dc_item("C", 2, false, string_nominal("AB"), 12),
                dc_item("X", 1, false, string_nominal("C1"), 14),
                dc_item("F", 4, false, string_nominal("-1"), 16),
                dc_item("P", 3, true, string_nominal("12"), 20),
                dc_item("A", 4, false, addresses(reloc("HELLO", 8)), 24),
                dc_item("V", 4, false, addresses({ { "kind", "EXTERNAL" }, { "name", "EXT" } }), 28),
            } },
        { "origin", open_code(r.source_uri, 6) },
    };
    const nlohmann::json expected_ds = {
        { "kind", "DS" },
        { "section", "HELLO" },
        { "offset", 32 },
        { "label", "" },
        { "mnemonic", "DS" },
        { "items", { ds_item("H", 2, 2, 32) } },
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
        { { "section", "HELLO" }, { "offset", 16 }, { "item", dc_item("F", 4, false, string_nominal("5"), 16) } },
        { { "section", "HELLO" }, { "offset", 20 }, { "item", dc_item("A", 4, false, addresses(reloc("HELLO", 4)), 20) } },
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

TEST(program_export, exports_each_dc_operand_at_its_own_offset_with_its_modifiers)
{
    const auto program = exported(run_bridge("operand_offsets",
        "T        CSECT\n"
        "         DC    C'X'\n"
        "         DC    H'1',F'2',BL.4'1010',BL.4'0101',C'A',FS2'1.25',FE1'100'\n"
        "         DS    0F\n"
        "         DC    0F\n"
        "         END\n"));
    const auto& statements = program.at("statements");

    ASSERT_EQ(statements.size(), 4u);
    std::vector<nlohmann::json> placed;
    for (const auto& item : statements[1].at("items"))
        placed.push_back({ item["type"], item["offset"], item["bit_offset"], item["length"], item["length_unit"],
            item["scale"], item["exponent"] });
    const std::vector<nlohmann::json> expected = {
        { "H", 2, 0, 2, "BYTE", 0, 0 },
        { "F", 4, 0, 4, "BYTE", 0, 0 },
        { "B", 8, 0, 4, "BIT", 0, 0 },
        { "B", 8, 4, 4, "BIT", 0, 0 },
        { "C", 9, 0, 1, "BYTE", 0, 0 },
        { "F", 12, 0, 4, "BYTE", 2, 0 },
        { "F", 16, 0, 4, "BYTE", 0, 1 },
    };
    EXPECT_EQ(placed, expected);
    auto zero_fullwords = ds_item("F", 0, 4, 20);
    EXPECT_EQ(statements[2].at("items"), (nlohmann::json { zero_fullwords }));
    zero_fullwords["nominal"] = { { "kind", "OMITTED" } };
    EXPECT_EQ(statements[3].at("items"), (nlohmann::json { zero_fullwords }));
}

TEST(program_export, evaluates_the_location_counter_at_each_dc_operand)
{
    const auto program = exported(run_bridge("operand_loctr",
        "T        CSECT\n"
        "         DC    A(0),A(*)\n"
        "         DC    C'A',A(*),Y(*-T)\n"
        "         END\n"));
    const auto& statements = program.at("statements");

    const auto once = [](nlohmann::json value) {
        return nlohmann::json { { "kind", "ADDRESSES_PER_COPY" }, { "copies", { { std::move(value) } } } };
    };
    ASSERT_EQ(statements.size(), 2u);
    EXPECT_EQ(statements[0].at("items")[0].at("nominal"), addresses(absolute(0)));
    EXPECT_EQ(statements[0].at("items")[1].at("nominal"), once(reloc("T", 4)));
    EXPECT_EQ(statements[1].at("items")[1].at("nominal"), once(reloc("T", 12)));
    EXPECT_EQ(statements[1].at("items")[2].at("nominal"), once(absolute(16)));
}

TEST(program_export, resolves_s_constants_through_the_active_using)
{
    const auto program = exported(run_bridge("s_constants",
        "T        CSECT\n"
        "         USING T,12\n"
        "         DC    S(FIELD),S(8(5)),S(100)\n"
        "         DC    SY(FIELD)\n"
        "FIELD    DC    F'0'\n"
        "         END\n"));
    const auto& statements = program.at("statements");

    const auto based = [](int base, int displacement) {
        return nlohmann::json { { "kind", "BASE_DISPLACEMENT" }, { "base", base }, { "displacement", displacement } };
    };
    ASSERT_EQ(statements.size(), 3u);
    std::vector<nlohmann::json> values;
    for (const auto& item : statements[0].at("items"))
        values.push_back(item.at("nominal").at("values")[0]);
    EXPECT_EQ(values, (std::vector<nlohmann::json> { based(12, 12), based(5, 8), based(0, 100) }));
    EXPECT_EQ(statements[1].at("items")[0].at("nominal"), addresses(based(12, 12)));
}

TEST(program_export, exports_channel_command_words_with_their_operands)
{
    const auto program = exported(run_bridge("ccw",
        "T        CSECT\n"
        "         DC    C'X'\n"
        "READ     CCW   X'02',BUF,X'20',80\n"
        "         CCW1  2,BUF,0,4\n"
        "         CCW0  X'03',0,X'40',1\n"
        "BUF      DS    CL80\n"
        "         END\n"));
    const auto& statements = program.at("statements");

    ASSERT_EQ(statements.size(), 5u);
    const auto ccw = [](int offset, const std::string& label, const std::string& mnemonic, int code,
                         nlohmann::json address, int flags, int count, int line, const std::string& file) {
        return nlohmann::json {
            { "kind", "CCW" },
            { "section", "T" },
            { "offset", offset },
            { "label", label },
            { "mnemonic", mnemonic },
            { "command_code", code },
            { "address", std::move(address) },
            { "flags", flags },
            { "count", count },
            { "origin", open_code(file, line) },
        };
    };
    const auto file = statements[0].at("origin").at("file").get<std::string>();
    EXPECT_EQ(statements[1], ccw(8, "READ", "CCW", 2, reloc("T", 32), 32, 80, 3, file));
    EXPECT_EQ(statements[2], ccw(16, "", "CCW1", 2, reloc("T", 32), 0, 4, 4, file));
    EXPECT_EQ(statements[3], ccw(24, "", "CCW0", 3, absolute(0), 64, 1, 5, file));
}

TEST(program_export, exports_amode_and_rmode_as_written)
{
    const auto program = exported(run_bridge("modes",
        "T        CSECT\n"
        "T        AMODE 31\n"
        "T        RMODE ANY\n"
        "U        CSECT\n"
        "U        AMODE ANY64\n"
        "         END\n"));

    EXPECT_EQ(program.at("amodes"),
        (nlohmann::json { { { "name", "T" }, { "mode", "31" } }, { { "name", "U" }, { "mode", "ANY64" } } }));
    EXPECT_EQ(program.at("rmodes"), (nlohmann::json { { { "name", "T" }, { "mode", "ANY" } } }));
}

TEST(program_export, exports_the_macro_call_stack_of_each_diagnostic)
{
    const auto r = run_bridge("related",
        "         MACRO\n"
        "         INNER\n"
        "         MNOTE 4,'CAREFUL'\n"
        "         MEND\n"
        "         MACRO\n"
        "         OUTER\n"
        "         INNER\n"
        "         MEND\n"
        "         OUTER\n"
        "         END\n");
    const auto diagnostics = exported(r).at("diagnostics");

    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].at("line"), 3);
    EXPECT_EQ(diagnostics[0].at("related"),
        (nlohmann::json {
            { { "file", r.source_uri }, { "line", 7 } },
            { { "file", r.source_uri }, { "line", 9 } },
        }));
}

TEST(program_export, evaluates_the_location_counter_at_each_value_of_each_copy)
{
    const auto program = exported(run_bridge("value_loctr",
        "T        CSECT\n"
        "         BR    14\n"
        "         DC    2A(*),A(*,*)\n"
        "         DC    2A(*,*+2)\n"
        "         DC    2A(0,4)\n"
        "         END\n"));
    const auto& statements = program.at("statements");

    const auto per_copy = [](nlohmann::json copies) {
        return nlohmann::json { { "kind", "ADDRESSES_PER_COPY" }, { "copies", std::move(copies) } };
    };
    ASSERT_EQ(statements.size(), 4u);
    EXPECT_EQ(statements[1].at("items")[0].at("nominal"),
        per_copy({ nlohmann::json { reloc("T", 4) }, nlohmann::json { reloc("T", 8) } }));
    EXPECT_EQ(statements[1].at("items")[1].at("nominal"), per_copy({ { reloc("T", 12), reloc("T", 16) } }));
    EXPECT_EQ(statements[2].at("items")[0].at("nominal"),
        per_copy({ { reloc("T", 20), reloc("T", 26) }, { reloc("T", 28), reloc("T", 34) } }));
    EXPECT_EQ(statements[3].at("items")[0].at("nominal"),
        (nlohmann::json { { "kind", "ADDRESSES" }, { "values", { absolute(0), absolute(4) } } }));
}

TEST(program_export, refuses_a_duplicated_s_constant_whose_later_copy_leaves_the_using_range)
{
    const auto r = run_bridge("s_copy_range",
        "T        CSECT\n"
        "         USING T,12\n"
        "         ORG   T+4094\n"
        "         DC    2S(*)\n"
        "         END\n");

    EXPECT_EQ(r.exit_code, 2);
    EXPECT_EQ(r.out, "");
    const auto diagnostics = nlohmann::json::parse(r.err).at("diagnostics");
    ASSERT_EQ(diagnostics.size(), 1u);
    EXPECT_EQ(diagnostics[0].at("code"), "BRIDGE");
    EXPECT_EQ(diagnostics[0].at("message"), "S-type constant at line 4 is not addressable through a USING");
}
