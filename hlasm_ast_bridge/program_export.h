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

// Assembles one source file and exports its resolved statement stream, symbols, sections and literals as JSON.

#ifndef HLASM_AST_BRIDGE_PROGRAM_EXPORT_H
#define HLASM_AST_BRIDGE_PROGRAM_EXPORT_H

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "analyzer.h"
#include "checking/diagnostic_collector.h"
#include "checking/machine_check.h"
#include "context/ordinary_assembly/location_counter.h"
#include "context/ordinary_assembly/section.h"
#include "ebcdic_encoding.h"
#include "expressions/mach_expr_visitor.h"
#include "expressions/mach_operator.h"
#include "fs_parse_lib_provider.h"
#include "instruction_table_export.h"
#include "library_info_transitional.h"
#include "statement_collector.h"
#include "utils/path_conversions.h"
#include "utils/resource_location.h"

namespace hlasm_ast_bridge {

constexpr int program_format = 1;

using hp::context::section_kind;

struct source_naming
{
    std::string uri;
    std::string name;

    std::string file_of(const std::string& file) const { return !name.empty() && file == uri ? name : file; }
};

class program_exporter
{
    hp::context::ordinary_assembly_context& ord_ctx;
    const hp::library_info& lib_info;
    const source_naming& naming;
    std::set<std::string> externals;
    std::vector<nlohmann::json> literals;
    std::vector<hp::context::id_index> literal_ids;

    class literal_finder final : public hp::expressions::mach_expr_visitor
    {
    public:
        std::vector<const hp::expressions::mach_expr_literal*> found;

        void visit(const hp::expressions::mach_expr_constant&) override { }
        void visit(const hp::expressions::mach_expr_data_attr&) override { }
        void visit(const hp::expressions::mach_expr_data_attr_literal&) override { }
        void visit(const hp::expressions::mach_expr_symbol&) override { }
        void visit(const hp::expressions::mach_expr_location_counter&) override { }
        void visit(const hp::expressions::mach_expr_default&) override { }
        void visit(const hp::expressions::mach_expr_literal& expr) override { found.push_back(&expr); }
    };

public:
    program_exporter(
        hp::context::ordinary_assembly_context& ord_ctx, const hp::library_info& lib_info, const source_naming& naming)
        : ord_ctx(ord_ctx)
        , lib_info(lib_info)
        , naming(naming)
    { }

    nlohmann::json program(const statement_collector& collector, nlohmann::json diagnostics)
    {
        auto statements = nlohmann::json::array();
        for (const auto& s : collector.statements)
            statements.push_back(statement(s));
        for (const auto& s : sections_of(section_kind::EXTERNAL))
            externals.insert(s);
        for (const auto& s : sections_of(section_kind::WEAK_EXTERNAL))
            externals.insert(s);
        return {
            { "format", program_format },
            { "sections", sections() },
            { "symbols", symbols() },
            { "entry", entry(collector) },
            { "externals", externals },
            { "statements", statements },
            { "literals", literals },
            { "diagnostics", std::move(diagnostics) },
        };
    }

private:
    static nlohmann::json entry(const statement_collector& collector)
    {
        if (collector.entry_unrepresentable)
            throw std::runtime_error("END operand is not a symbol");
        if (collector.entry)
            return { { "kind", "NAMED" }, { "name", *collector.entry } };
        return { { "kind", "DEFAULT" } };
    }

    static std::string name_of(hp::context::id_index id) { return std::string(id.to_string_view()); }

    static const hp::context::section* owner_of(const hp::context::address& a) { return a.bases().front().owner; }

    std::vector<std::string> sections_of(section_kind kind) const
    {
        std::vector<std::string> names;
        for (const auto& s : ord_ctx.sections())
            if (s->kind == kind)
                names.push_back(name_of(s->name));
        return names;
    }

    static std::string_view section_kind_name(section_kind k)
    {
        switch (k)
        {
            case section_kind::DUMMY:
                return "DSECT";
            case section_kind::COMMON:
                return "COM";
            case section_kind::EXECUTABLE:
                return "CSECT";
            case section_kind::READONLY:
                return "RSECT";
            default:
                return "";
        }
    }

    nlohmann::json sections()
    {
        auto result = nlohmann::json::array();
        for (const auto& s : ord_ctx.sections())
        {
            if (section_kind_name(s->kind).empty())
                continue;
            int length = 0;
            for (const auto& l : s->location_counters())
            {
                (void)l->set_available_value();
                length = std::max(length, l->current_address().offset());
            }
            result.push_back(
                { { "name", name_of(s->name) }, { "kind", section_kind_name(s->kind) }, { "length", length } });
        }
        return result;
    }

    nlohmann::json symbols() const
    {
        std::vector<nlohmann::json> rows;
        for (const auto& [id, entry] : ord_ctx.symbols())
        {
            const auto* sym = std::get_if<hp::context::symbol>(&entry);
            if (!sym || std::ranges::find(literal_ids, id) != literal_ids.end())
                continue;
            const auto& attrs = sym->attributes();
            nlohmann::json row = {
                { "name", name_of(id) },
                { "type", hp::ebcdic_encoding::to_ascii(attrs.type()) },
                { "length", attrs.length() },
            };
            if (sym->kind() == hp::context::symbol_value_kind::ABS)
            {
                row["kind"] = "ABS";
                row["value"] = sym->value().get_abs();
            }
            else if (sym->kind() == hp::context::symbol_value_kind::RELOC && sym->value().get_reloc().is_simple())
            {
                const auto& reloc = sym->value().get_reloc();
                row["kind"] = "RELOC";
                row["section"] = name_of(owner_of(reloc)->name);
                row["offset"] = reloc.offset();
            }
            else
                continue;
            rows.push_back(std::move(row));
        }
        std::ranges::sort(rows, { }, [](const nlohmann::json& r) { return r.at("name").get<std::string>(); });
        return rows;
    }

    nlohmann::json statement(const recorded_statement& s)
    {
        hp::context::ordinary_assembly_dependency_solver solver(ord_ctx, s.dep_ctx, lib_info);
        nlohmann::json row = {
            { "section", name_of(owner_of(s.address)->name) },
            { "offset", s.address.offset() },
            { "label", s.label },
            { "mnemonic", s.mnemonic },
            { "origin", origin(s.origin) },
        };
        if (const auto* m = std::get_if<machine_body>(&s.body))
        {
            row["kind"] = "MACHINE";
            row["base_mnemonic"] = m->instruction->name();
            row["format"] = hp::instructions::mach_format_to_string(m->instruction->format());
            row["length"] = m->instruction->size_in_bits() / 8;
            row["operands"] = machine_operands(*m, s, solver);
            collect_literals(*m, solver);
            return row;
        }
        const auto& data = std::get<data_body>(s.body);
        row["kind"] = data.initialised ? "DC" : "DS";
        row["items"] = nlohmann::json::array();
        for (const auto& d : data.definitions)
            row["items"].push_back(data_item(*d, data.initialised, solver));
        return row;
    }

    nlohmann::json origin(const statement_origin& o) const
    {
        if (!o.macro)
            return { { "kind", "OPEN_CODE" }, { "file", naming.file_of(o.file) }, { "line", o.line } };
        return {
            { "kind", "MACRO" },
            { "file", naming.file_of(o.file) },
            { "line", o.line },
            { "macro", *o.macro },
            { "depth", o.depth },
        };
    }

    nlohmann::json machine_operands(
        const machine_body& m, const recorded_statement& s, hp::context::dependency_solver& solver) const
    {
        using hp::checking::machine_operand;
        constexpr auto max = hp::instructions::machine_instruction::max_operand_count;
        std::array<machine_operand, max> evaluated;
        std::array<machine_operand, max> transformed;
        hp::diagnostic_collector silent;
        const auto formats = m.instruction->operands();
        const auto* end = hp::checking::evaluate_operands(
            evaluated.data(), *m.instruction, s.mnemonic, m.operands, formats, m.transforms, solver, silent);
        if (!end)
            throw std::runtime_error(
                "operands of " + s.mnemonic + " at line " + std::to_string(s.origin.line) + " did not evaluate");
        const auto* t_end = hp::checking::apply_transforms(
            transformed.data(), std::span<const machine_operand>(evaluated.data(), end), formats.size(), m.transforms);

        auto result = nlohmann::json::array();
        for (const auto* op = transformed.data(); op != t_end; ++op)
            result.push_back(machine_operand_json(*op, formats[op - transformed.data()], m, solver));
        return result;
    }

    nlohmann::json machine_operand_json(const hp::checking::machine_operand& op,
        const hp::instructions::machine_operand_format& fmt,
        const machine_body& m,
        hp::context::dependency_solver& solver) const
    {
        using enum hp::instructions::machine_operand_type;
        const auto type = fmt.identifier.type;
        if ((type == DISP || type == DISP_IDX) && fmt.first.type == LENGTH)
            return {
                { "kind", "STORAGE_LENGTH" },
                { "displacement", op.displacement },
                { "length", op.first_op },
                { "base", op.second_op },
            };
        if (type == DISP || type == DISP_IDX)
            return {
                { "kind", "STORAGE" },
                { "displacement", op.displacement },
                { "index", fmt.first.type == NONE ? 0 : op.first_op },
                { "base", op.second_op },
                { "written", written_address(op, m, solver) },
            };
        if (type == RELOC_IMM)
        {
            const auto* mop = m.operands[op.source]->access_mach();
            const auto* rel = dynamic_cast<const hp::expressions::mach_expr_binary<hp::expressions::rel_addr>*>(
                mop->displacement.get());
            const auto target =
                rel ? rel->right_expression()->evaluate(solver, hp::drop_diagnostic_op) : hp::context::symbol_value();
            if (target.value_kind() == hp::context::symbol_value_kind::RELOC && target.get_reloc().is_simple())
                return {
                    { "kind", "RELOC_IMM" },
                    { "section", name_of(owner_of(target.get_reloc())->name) },
                    { "offset", target.get_reloc().offset() },
                };
            return { { "kind", "IMM" }, { "value", op.displacement } };
        }
        return { { "kind", operand_type_name(type) }, { "value", op.displacement } };
    }

    nlohmann::json written_address(const hp::checking::machine_operand& op,
        const machine_body& m,
        hp::context::dependency_solver& solver) const
    {
        const auto v =
            m.operands[op.source]->access_mach()->displacement->evaluate(solver, hp::drop_diagnostic_op);
        if (v.value_kind() != hp::context::symbol_value_kind::RELOC || !v.get_reloc().is_simple())
            return { { "kind", "ABS" }, { "value", op.displacement } };
        return {
            { "kind", "RELOC" },
            { "section", name_of(owner_of(v.get_reloc())->name) },
            { "offset", v.get_reloc().offset() },
        };
    }

    void collect_literals(const machine_body& m, hp::context::dependency_solver& solver)
    {
        literal_finder finder;
        for (const auto& o : m.operands)
            o->access_mach()->apply_mach_visitor(finder);
        for (const auto* lit : finder.found)
        {
            const auto id = lit->get_literal_id(solver);
            if (std::ranges::find(literal_ids, id) != literal_ids.end())
                continue;
            literal_ids.push_back(id);
            const auto& address = ord_ctx.get_symbol(id)->value().get_reloc();
            literals.push_back({
                { "section", name_of(owner_of(address)->name) },
                { "offset", address.offset() },
                { "item", data_item(lit->get_data_definition(), true, solver) },
            });
        }
    }

    nlohmann::json data_item(
        const hp::expressions::data_definition& d, bool with_nominal, hp::context::dependency_solver& solver)
    {
        const auto dupl = d.evaluate_dupl_factor(solver, hp::drop_diagnostic_op);
        nlohmann::json item = {
            { "type", std::string(1, d.type) },
            { "extension", d.extension ? std::string(1, d.extension) : std::string() },
            { "duplication", dupl.present ? dupl.value : 1 },
            { "length", d.get_length_attribute(solver, hp::drop_diagnostic_op) },
            { "explicit_length", d.length != nullptr },
        };
        if (with_nominal)
            item["nominal"] = nominal(d, solver);
        return item;
    }

    nlohmann::json nominal(const hp::expressions::data_definition& d, hp::context::dependency_solver& solver)
    {
        if (const auto* s = d.nominal_value->access_string())
            return { { "kind", "STRING" }, { "value", s->value } };
        auto values = nlohmann::json::array();
        for (const auto& a : d.nominal_value->access_exprs()->exprs)
            values.push_back(address_value(d.type, *a.displacement, solver));
        return { { "kind", "ADDRESSES" }, { "values", values } };
    }

    nlohmann::json address_value(
        char type, const hp::expressions::mach_expression& e, hp::context::dependency_solver& solver)
    {
        const auto* symbol = dynamic_cast<const hp::expressions::mach_expr_symbol*>(&e);
        if (type == 'V' && symbol)
            return external(name_of(symbol->value));
        const auto v = e.evaluate(solver, hp::drop_diagnostic_op);
        if (v.value_kind() == hp::context::symbol_value_kind::ABS)
            return { { "kind", "ABS" }, { "value", v.get_abs() } };
        if (v.value_kind() != hp::context::symbol_value_kind::RELOC || !v.get_reloc().is_simple())
            throw std::runtime_error("address constant at " + std::to_string(e.get_range().start.line + 1)
                + " is neither absolute nor simply relocatable");
        const auto* owner = owner_of(v.get_reloc());
        if (owner->kind == section_kind::EXTERNAL || owner->kind == section_kind::WEAK_EXTERNAL)
            return external(name_of(owner->name));
        return { { "kind", "RELOC" }, { "section", name_of(owner->name) }, { "offset", v.get_reloc().offset() } };
    }

    nlohmann::json external(std::string name)
    {
        externals.insert(name);
        return { { "kind", "EXTERNAL" }, { "name", std::move(name) } };
    }
};

inline std::string_view severity_name(hp::diagnostic_severity s)
{
    switch (s)
    {
        case hp::diagnostic_severity::error:
            return "ERROR";
        case hp::diagnostic_severity::warning:
            return "WARNING";
        case hp::diagnostic_severity::info:
            return "INFO";
        case hp::diagnostic_severity::hint:
            return "HINT";
        default:
            return "UNSPECIFIED";
    }
}

inline nlohmann::json diagnostic_json(
    std::string_view code, std::string_view severity, std::string_view message, std::string_view file, size_t line)
{
    return {
        { "code", code },
        { "severity", severity },
        { "message", message },
        { "file", file },
        { "line", line },
    };
}

inline nlohmann::json diagnostics_json(std::span<const hp::diagnostic> diags, const source_naming& naming)
{
    auto result = nlohmann::json::array();
    for (const auto& d : diags)
        result.push_back(
            diagnostic_json(d.code, severity_name(d.severity), d.message, naming.file_of(d.file_uri), d.diag_range.start.line + 1));
    return result;
}

inline int refuse(std::ostream& err, nlohmann::json diagnostics)
{
    err << nlohmann::json { { "diagnostics", std::move(diagnostics) } }.dump() << '\n';
    return 2;
}

inline int export_program(const std::string& source_path,
    const std::string& source_name,
    const std::vector<std::string>& syslib,
    std::ostream& out,
    std::ostream& err)
{
    std::ifstream in(source_path, std::ios::binary);
    if (!in)
    {
        err << "cannot read " << source_path << '\n';
        return 1;
    }
    const std::string source { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };

    hp::fs_parse_lib_provider provider(syslib);
    hp::library_info_transitional lib_info(provider);
    const auto source_uri =
        hlasm_plugin::utils::path::path_to_uri(std::filesystem::absolute(source_path).lexically_normal().string());
    const source_naming naming { source_uri, source_name };
    hp::analyzer a(source,
        hp::analyzer_options {
            hlasm_plugin::utils::resource::resource_location(source_uri),
            &provider,
            hp::file_is_opencode::yes,
        });
    statement_collector collector(a.hlasm_ctx(), lib_info);
    a.register_stmt_analyzer(&collector);
    a.analyze();

    const auto diags = a.diags();
    auto diagnostics = diagnostics_json(diags, naming);
    if (std::ranges::any_of(diags, [](const auto& d) { return d.severity == hp::diagnostic_severity::error; }))
        return refuse(err, std::move(diagnostics));

    try
    {
        program_exporter exporter(a.hlasm_ctx().ord_ctx, lib_info, naming);
        out << exporter.program(collector, std::move(diagnostics)).dump() << '\n';
        return 0;
    }
    catch (const std::runtime_error& e)
    {
        return refuse(err, nlohmann::json::array({ diagnostic_json("BRIDGE", "ERROR", e.what(), naming.file_of(source_uri), 0) }));
    }
}

inline int run(std::span<const std::string> args, std::ostream& out, std::ostream& err)
{
    if (args.size() == 1 && args.front() == "--instruction-table")
    {
        export_instruction_table(out);
        return 0;
    }
    std::vector<std::string> sources;
    std::vector<std::string> syslib;
    std::string source_name;
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--syslib" && i + 1 < args.size())
            syslib.push_back(args[++i]);
        else if (args[i] == "--source-name" && i + 1 < args.size())
            source_name = args[++i];
        else
            sources.push_back(args[i]);
    }
    if (sources.size() != 1 || sources.front().starts_with("--"))
    {
        err << "usage: hlasm_ast_bridge --instruction-table | hlasm_ast_bridge <src> [--source-name name] [--syslib dir]...\n";
        return 1;
    }
    return export_program(sources.front(), source_name, syslib, out, err);
}

} // namespace hlasm_ast_bridge

#endif
