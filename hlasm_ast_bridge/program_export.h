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
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ostream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "analyzer.h"
#include "checking/data_definition/data_def_type_base.h"
#include "checking/diagnostic_collector.h"
#include "checking/machine_check.h"
#include "context/ordinary_assembly/location_counter.h"
#include "context/ordinary_assembly/section.h"
#include "context/using.h"
#include "ebcdic_encoding.h"
#include "expressions/mach_expr_visitor.h"
#include "expressions/mach_operator.h"
#include "fs_parse_lib_provider.h"
#include "instruction_table_export.h"
#include "processing/instruction_sets/data_def_postponed_statement.h"
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
    std::vector<pool_point> pools;

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
        if (collector.unrepresentable)
            throw std::runtime_error(*collector.unrepresentable);
        pools = collector.pools;
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
            { "amodes", modes(collector.amodes) },
            { "rmodes", modes(collector.rmodes) },
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

    static nlohmann::json modes(const std::vector<mode_setting>& settings)
    {
        auto result = nlohmann::json::array();
        for (const auto& m : settings)
            result.push_back({ { "name", m.name }, { "mode", m.mode } });
        return result;
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
            literal_finder finder;
            for (const auto& o : m->operands)
                o->access_mach()->apply_mach_visitor(finder);
            collect_literals(finder, s, solver);
            return row;
        }
        if (const auto* c = std::get_if<ccw_body>(&s.body))
        {
            row["kind"] = "CCW";
            row["command_code"] = absolute_operand(*c->operands[0], solver);
            row["address"] = address_value(*c->operands[1], solver);
            row["flags"] = absolute_operand(*c->operands[2], solver);
            row["count"] = absolute_operand(*c->operands[3], solver);
            literal_finder finder;
            for (const auto& o : c->operands)
                o->apply(finder);
            collect_literals(finder, s, solver);
            return row;
        }
        const auto& data = std::get<data_body>(s.body);
        row["kind"] = data.initialised ? "DC" : "DS";
        row["items"] = data_items(data, s.address, solver);
        return row;
    }

    static int absolute_operand(const hp::expressions::mach_expression& e, hp::context::dependency_solver& solver)
    {
        const auto v = e.evaluate(solver, hp::drop_diagnostic_op);
        if (v.value_kind() != hp::context::symbol_value_kind::ABS)
            throw std::runtime_error("operand at " + std::to_string(e.get_range().start.line + 1) + " is not absolute");
        return v.get_abs();
    }

    static int modifier(const hp::expressions::mach_expr_ptr& e, hp::context::dependency_solver& solver)
    { return e ? absolute_operand(*e, solver) : 0; }

    nlohmann::json data_items(
        const data_body& data, const hp::context::address& start, hp::context::dependency_solver& solver)
    {
        using enum hp::expressions::data_definition::length_type;
        const auto instr = data.initialised ? hp::checking::data_instr_type::DC : hp::checking::data_instr_type::DS;
        const uint64_t start_bit = uint64_t(start.offset()) * 8;
        auto bit = start_bit;
        auto items = nlohmann::json::array();
        for (const auto& d : data.definitions)
        {
            if (d->length_type != BIT)
                bit = hp::checking::round_up(
                    hp::checking::round_up(bit, uint64_t { 8 }), uint64_t(d->get_alignment().boundary) * 8);
            const auto at = start + int((bit - start_bit) / 8);
            hp::processing::data_def_dependency_solver at_operand(solver, &at);
            items.push_back(data_item(*d, data.initialised, at_operand, at.offset(), bit % 8, &at));
            bit += d->evaluate_total_length(at_operand, instr, hp::drop_diagnostic_op);
        }
        return items;
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

    hp::index_t<hp::context::using_collection> pool_using(
        const hp::context::address& literal, hp::index_t<hp::context::using_collection> fallback) const
    {
        const pool_point* found = nullptr;
        for (const auto& p : pools)
            if (p.section == owner_of(literal) && p.offset <= literal.offset() && (!found || p.offset >= found->offset))
                found = &p;
        return found ? found->active_using : fallback;
    }

    void collect_literals(
        const literal_finder& finder, const recorded_statement& s, hp::context::dependency_solver& solver)
    {
        for (const auto* lit : finder.found)
        {
            const auto id = lit->get_literal_id(solver);
            if (std::ranges::find(literal_ids, id) != literal_ids.end())
                continue;
            literal_ids.push_back(id);
            const auto& address = ord_ctx.get_symbol(id)->value().get_reloc();
            auto at_pool = s.dep_ctx;
            at_pool.active_using = pool_using(address, s.dep_ctx.active_using);
            hp::context::ordinary_assembly_dependency_solver pool_solver(ord_ctx, at_pool, lib_info);
            literals.push_back({
                { "section", name_of(owner_of(address)->name) },
                { "offset", address.offset() },
                { "item", data_item(lit->get_data_definition(), true, pool_solver, address.offset(), 0) },
                { "origin", origin(s.origin) },
            });
        }
    }

    nlohmann::json data_item(const hp::expressions::data_definition& d,
        bool with_nominal,
        hp::context::dependency_solver& solver,
        int offset,
        uint64_t bit_offset,
        const hp::context::address* operand = nullptr)
    {
        const auto dupl = d.evaluate_dupl_factor(solver, hp::drop_diagnostic_op);
        const bool in_bits = d.length_type == hp::expressions::data_definition::length_type::BIT;
        const int length =
            in_bits ? modifier(d.length, solver) : int(d.get_length_attribute(solver, hp::drop_diagnostic_op));
        nlohmann::json item = {
            { "type", std::string(1, d.type) },
            { "extension", d.extension ? std::string(1, d.extension) : std::string() },
            { "duplication", dupl.present ? dupl.value : 1 },
            { "length", length },
            { "length_unit", in_bits ? "BIT" : "BYTE" },
            { "explicit_length", d.length != nullptr },
            { "scale", modifier(d.scale, solver) },
            { "exponent", modifier(d.exponent, solver) },
            { "offset", offset },
            { "bit_offset", bit_offset },
        };
        if (with_nominal)
            item["nominal"] = nominal(d, solver, operand, length);
        return item;
    }

    nlohmann::json nominal(const hp::expressions::data_definition& d,
        hp::context::dependency_solver& solver,
        const hp::context::address* operand,
        int length)
    {
        if (!d.nominal_value)
            return { { "kind", "OMITTED" } };
        if (const auto* s = d.nominal_value->access_string())
            return { { "kind", "STRING" }, { "value", s->value } };
        const auto& exprs = d.nominal_value->access_exprs()->exprs;
        if (operand && d.references_loctr)
            return { { "kind", "ADDRESSES_PER_COPY" }, { "copies", copies(d, exprs, solver, *operand, length) } };
        auto values = nlohmann::json::array();
        for (const auto& a : exprs)
            values.push_back(nominal_address(d, a, solver));
        return { { "kind", "ADDRESSES" }, { "values", values } };
    }

    nlohmann::json copies(const hp::expressions::data_definition& d,
        const std::vector<hp::expressions::address_nominal>& exprs,
        hp::context::dependency_solver& solver,
        const hp::context::address& operand,
        int length)
    {
        const auto dupl = d.evaluate_dupl_factor(solver, hp::drop_diagnostic_op);
        const int count = dupl.present ? dupl.value : 1;
        auto result = nlohmann::json::array();
        for (int copy = 0; copy < count; ++copy)
        {
            auto values = nlohmann::json::array();
            for (size_t k = 0; k < exprs.size(); ++k)
            {
                const auto at = operand + int((copy * exprs.size() + k) * length);
                hp::processing::data_def_dependency_solver at_value(solver, &at);
                values.push_back(nominal_address(d, exprs[k], at_value));
            }
            result.push_back(std::move(values));
        }
        return result;
    }

    nlohmann::json nominal_address(const hp::expressions::data_definition& d,
        const hp::expressions::address_nominal& a,
        hp::context::dependency_solver& solver)
    {
        const auto* symbol = dynamic_cast<const hp::expressions::mach_expr_symbol*>(a.displacement.get());
        if (d.type == 'V' && symbol)
            return external(name_of(symbol->value));
        if (d.type == 'S')
            return base_displacement(d, a, solver);
        return address_value(*a.displacement, solver);
    }

    static nlohmann::json based(int base, int displacement)
    { return { { "kind", "BASE_DISPLACEMENT" }, { "base", base }, { "displacement", displacement } }; }

    static nlohmann::json base_displacement(const hp::expressions::data_definition& d,
        const hp::expressions::address_nominal& a,
        hp::context::dependency_solver& solver)
    {
        if (a.base)
            return based(absolute_operand(*a.base, solver), absolute_operand(*a.displacement, solver));
        const auto v = a.displacement->evaluate(solver, hp::drop_diagnostic_op);
        if (v.value_kind() == hp::context::symbol_value_kind::ABS)
            return based(0, v.get_abs());
        const auto& reloc = v.get_reloc();
        const auto& base = reloc.bases().front();
        const auto resolved = solver.using_evaluate(base.qualifier, base.owner, reloc.offset(), d.extension == 'Y');
        if (resolved.reg == hp::context::using_collection::invalid_register)
            throw std::runtime_error("S-type constant at line " + std::to_string(a.total.start.line + 1)
                + " is not addressable through a USING");
        return based(resolved.reg, resolved.reg_offset);
    }

    nlohmann::json address_value(const hp::expressions::mach_expression& e, hp::context::dependency_solver& solver)
    {
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

inline nlohmann::json diagnostic_json(std::string_view code,
    std::string_view severity,
    std::string_view message,
    std::string_view file,
    size_t line,
    nlohmann::json related)
{
    return {
        { "code", code },
        { "severity", severity },
        { "message", message },
        { "file", file },
        { "line", line },
        { "related", std::move(related) },
    };
}

inline nlohmann::json related_json(const hp::diagnostic& d, const source_naming& naming)
{
    auto result = nlohmann::json::array();
    for (const auto& r : d.related)
        result.push_back({ { "file", naming.file_of(r.location.uri) }, { "line", r.location.rang.start.line + 1 } });
    return result;
}

inline nlohmann::json diagnostics_json(std::span<const hp::diagnostic> diags, const source_naming& naming)
{
    auto result = nlohmann::json::array();
    for (const auto& d : diags)
        result.push_back(diagnostic_json(d.code,
            severity_name(d.severity),
            d.message,
            naming.file_of(d.file_uri),
            d.diag_range.start.line + 1,
            related_json(d, naming)));
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
    hp::asm_option inputs,
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
            std::move(inputs),
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
        return refuse(err, nlohmann::json::array({ diagnostic_json("BRIDGE", "ERROR", e.what(), naming.file_of(source_uri), 0, nlohmann::json::array()) }));
    }
}

inline std::optional<hp::assembly_clock> parse_clock(const std::string& text)
{
    using namespace std::chrono;
    int y, mo, d, h, mi, sec, micro, oh, om;
    char sign;
    char rest;
    if (text.size() != 32
        || std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d.%6d%c%2d:%2d%c", &y, &mo, &d, &h, &mi, &sec, &micro, &sign, &oh, &om, &rest)
            != 10
        || (sign != '+' && sign != '-') || h > 23 || mi > 59 || sec > 59 || oh > 23 || om > 59)
        return std::nullopt;
    const year_month_day date { year(y), month(unsigned(mo)), day(unsigned(d)) };
    if (!date.ok())
        return std::nullopt;
    const auto offset = (sign == '+' ? 1 : -1) * (hours(oh) + minutes(om));
    const auto utc = sys_days(date) + hours(h) + minutes(mi) + seconds(sec) - offset;
    const auto utc_day = floor<days>(utc);
    const year_month_day utc_date(utc_day);
    const hh_mm_ss utc_time(utc - utc_day);
    return hp::assembly_clock {
        .local = hlasm_plugin::utils::timestamp(y, mo, d, h, mi, sec, micro),
        .utc = hlasm_plugin::utils::timestamp(int(utc_date.year()),
            unsigned(utc_date.month()),
            unsigned(utc_date.day()),
            unsigned(utc_time.hours().count()),
            unsigned(utc_time.minutes().count()),
            unsigned(utc_time.seconds().count()),
            micro),
    };
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
    hp::asm_option inputs { .sysin_dsn = std::nullopt, .sysin_member = std::nullopt };
    for (size_t i = 0; i < args.size(); ++i)
    {
        if (args[i] == "--syslib" && i + 1 < args.size())
            syslib.push_back(args[++i]);
        else if (args[i] == "--sysparm" && i + 1 < args.size())
            inputs.sysparm = args[++i];
        else if (args[i] == "--sysjob" && i + 1 < args.size())
            inputs.sysjob = args[++i];
        else if (args[i] == "--sysstep" && i + 1 < args.size())
            inputs.sysstep = args[++i];
        else if (args[i] == "--sysin-dsn" && i + 1 < args.size())
            inputs.sysin_dsn = args[++i];
        else if (args[i] == "--sysin-member" && i + 1 < args.size())
            inputs.sysin_member = args[++i];
        else if (args[i] == "--clock" && i + 1 < args.size())
        {
            const auto& text = args[++i];
            inputs.clock = parse_clock(text);
            if (!inputs.clock)
            {
                err << "--clock takes YYYY-MM-DDTHH:MM:SS.ffffff+HH:MM, not " << text << '\n';
                return 1;
            }
        }
        else if (args[i] == "--source-name" && i + 1 < args.size())
            source_name = args[++i];
        else
            sources.push_back(args[i]);
    }
    if (sources.size() != 1 || sources.front().starts_with("--"))
    {
        err << "usage: hlasm_ast_bridge --instruction-table | hlasm_ast_bridge <src> [--source-name name] [--syslib dir]... "
               "[--sysparm v] [--sysjob v] [--sysstep v] [--sysin-dsn v] [--sysin-member v] [--clock iso-instant]\n";
        return 1;
    }
    return export_program(sources.front(), source_name, syslib, std::move(inputs), out, err);
}

} // namespace hlasm_ast_bridge

#endif
