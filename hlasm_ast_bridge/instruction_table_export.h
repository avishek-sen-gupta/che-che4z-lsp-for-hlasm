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

// Exports che4z's machine-instruction and extended-mnemonic tables as JSON.

#ifndef HLASM_AST_BRIDGE_INSTRUCTION_TABLE_EXPORT_H
#define HLASM_AST_BRIDGE_INSTRUCTION_TABLE_EXPORT_H

#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "instructions/instruction.h"

namespace hlasm_ast_bridge {

namespace instructions = hlasm_plugin::parser_library::instructions;

constexpr int instruction_table_format = 1;

inline std::string_view operand_type_name(instructions::machine_operand_type t)
{
    using enum instructions::machine_operand_type;
    switch (t)
    {
        case NONE:
            return "NONE";
        case MASK:
            return "MASK";
        case REG:
            return "REG";
        case IMM:
            return "IMM";
        case DISP:
            return "DISP";
        case DISP_IDX:
            return "DISP_IDX";
        case BASE:
            return "BASE";
        case LENGTH:
            return "LENGTH";
        case VEC_REG:
            return "VEC_REG";
        case IDX_REG:
            return "IDX_REG";
        case RELOC_IMM:
            return "RELOC_IMM";
    }
    return "NONE";
}

inline std::string_view privilege_name(instructions::privilege_status p)
{
    using enum instructions::privilege_status;
    switch (p)
    {
        case not_privileged:
            return "not_privileged";
        case privileged:
            return "privileged";
        case conditionally_privileged:
            return "conditionally_privileged";
    }
    return "not_privileged";
}

inline std::string_view z_arch_name(instructions::z_arch_affiliation z)
{
    using enum instructions::z_arch_affiliation;
    switch (z)
    {
        case NO_AFFILIATION:
            return "";
        case ZOP:
            return "ZOP";
        case YOP:
            return "YOP";
        case Z9:
            return "Z9";
        case Z10:
            return "Z10";
        case Z11:
            return "Z11";
        case Z12:
            return "Z12";
        case Z13:
            return "Z13";
        case Z14:
            return "Z14";
        case Z15:
            return "Z15";
        case Z16:
            return "Z16";
        case Z17:
            return "Z17";
        case LAST:
            return "LAST";
    }
    return "";
}

inline std::string transform_kind_name(instructions::mnemonic_transformation_kind k)
{
    using enum instructions::mnemonic_transformation_kind;
    switch (k)
    {
        case value:
            return "value";
        case copy:
            return "copy";
        case or_with:
            return "or_with";
        case add_to:
            return "add_to";
        case subtract_from:
            return "subtract_from";
        case complement:
            return "complement";
    }
    return "value";
}

inline std::string arch_name(const instructions::instruction_set_affiliation& a)
{
    using enum instructions::z_arch_affiliation;
    std::vector<std::string_view> parts;
    if (a.uni)
        parts.push_back("UNI");
    if (a.esa)
        parts.push_back("ESA");
    if (a.xa)
        parts.push_back("XA");
    if (a._370)
        parts.push_back("370");
    if (a.dos)
        parts.push_back("DOS");
    if (a.z_arch != NO_AFFILIATION)
        parts.insert(parts.end(), { "SINCE", z_arch_name(a.z_arch) });
    if (a.z_arch_removed != NO_AFFILIATION && a.z_arch_removed != LAST)
        parts.insert(parts.end(), { "TILL", z_arch_name(a.z_arch_removed) });
    std::string name;
    for (const auto part : parts)
        name.append(name.empty() ? "" : "_").append(part);
    return name;
}

inline nlohmann::json parameter_json(const instructions::parameter& p)
{
    return {
        { "kind", operand_type_name(p.type) },
        { "bits", static_cast<int>(p.size) },
        { "signed", static_cast<bool>(p.is_signed) },
    };
}

inline nlohmann::json operand_json(const instructions::machine_operand_format& op)
{
    auto row = parameter_json(op.identifier);
    row["first"] = parameter_json(op.first);
    row["second"] = parameter_json(op.second);
    row["optional"] = op.optional;
    return row;
}

inline nlohmann::json operands_json(std::span<const instructions::machine_operand_format> ops)
{
    auto list = nlohmann::json::array();
    for (const auto& op : ops)
        list.push_back(operand_json(op));
    return list;
}

inline nlohmann::json transform_json(const instructions::mnemonic_transformation& t)
{
    return {
        { "kind", transform_kind_name(t.type) },
        { "skip", static_cast<int>(t.skip) },
        { "value", static_cast<int>(t.value) },
        { "source", static_cast<int>(t.source) },
        { "insert", static_cast<bool>(t.insert) },
    };
}

inline nlohmann::json instruction_row(const instructions::machine_instruction& mi)
{
    return {
        { "mnemonic", mi.name() },
        { "format", instructions::mach_format_to_string(mi.format()) },
        { "privileged", privilege_name(mi.privileged()) },
        { "arch", arch_name(mi.instr_set_affiliation()) },
        { "operands", operands_json(mi.operands()) },
        { "base", nullptr },
        { "transforms", nlohmann::json::array() },
    };
}

inline nlohmann::json mnemonic_row(const instructions::mnemonic_code& mn)
{
    const auto& base = mn.instruction();
    auto transforms = nlohmann::json::array();
    for (const auto& t : mn.operand_transformations())
        transforms.push_back(transform_json(t));
    return {
        { "mnemonic", mn.name() },
        { "format", instructions::mach_format_to_string(base.format()) },
        { "privileged", privilege_name(base.privileged()) },
        { "arch", arch_name(mn.instr_set_affiliation()) },
        { "operands", operands_json(base.operands()) },
        { "base", base.name() },
        { "transforms", transforms },
    };
}

inline nlohmann::json instruction_table_json()
{
    auto rows = nlohmann::json::array();
    for (const auto& mi : instructions::all_machine_instructions())
        rows.push_back(instruction_row(mi));
    for (const auto& mn : instructions::all_mnemonic_codes())
        rows.push_back(mnemonic_row(mn));
    return { { "format", instruction_table_format }, { "instructions", rows } };
}

inline void export_instruction_table(std::ostream& out) { out << instruction_table_json().dump() << '\n'; }

} // namespace hlasm_ast_bridge

#endif
