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

// Records every ordinary machine and DC/DS statement with the context needed to evaluate it after assembly.

#ifndef HLASM_AST_BRIDGE_STATEMENT_COLLECTOR_H
#define HLASM_AST_BRIDGE_STATEMENT_COLLECTOR_H

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "context/hlasm_context.h"
#include "context/ordinary_assembly/ordinary_assembly_dependency_solver.h"
#include "context/ordinary_assembly/symbol_dependency_tables.h"
#include "expressions/data_definition.h"
#include "expressions/mach_expr_term.h"
#include "expressions/nominal_value.h"
#include "instructions/instruction.h"
#include "library_info.h"
#include "processing/statement.h"
#include "processing/statement_analyzers/statement_analyzer.h"
#include "semantics/operand_impls.h"

namespace hlasm_ast_bridge {

namespace hp = hlasm_plugin::parser_library;

struct statement_origin
{
    std::string file;
    size_t line;
    std::optional<std::string> macro;
    size_t depth;
};

struct machine_body
{
    const hp::instructions::machine_instruction* instruction;
    std::span<const hp::instructions::mnemonic_transformation> transforms;
    std::vector<std::unique_ptr<hp::semantics::operand>> operands;
};

struct data_body
{
    bool initialised;
    std::vector<std::unique_ptr<hp::expressions::data_definition>> definitions;
};

struct recorded_statement
{
    std::string mnemonic;
    std::string label;
    hp::context::address address;
    hp::context::dependency_evaluation_context dep_ctx;
    statement_origin origin;
    std::variant<machine_body, data_body> body;
};

inline hp::expressions::mach_expr_ptr clone_expr(const hp::expressions::mach_expr_ptr& e)
{ return e ? e->clone() : nullptr; }

inline hp::expressions::nominal_value_ptr clone_nominal(const hp::expressions::nominal_value_ptr& n)
{
    if (!n)
        return nullptr;
    if (const auto* s = n->access_string())
        return std::make_unique<hp::expressions::nominal_value_string>(s->value, s->value_range);
    std::vector<hp::expressions::address_nominal> exprs;
    for (const auto& a : n->access_exprs()->exprs)
        exprs.emplace_back(clone_expr(a.displacement), clone_expr(a.base), a.total);
    return std::make_unique<hp::expressions::nominal_value_exprs>(std::move(exprs), n->value_range);
}

inline std::unique_ptr<hp::expressions::data_definition> clone_definition(const hp::expressions::data_definition& d)
{
    auto c = std::make_unique<hp::expressions::data_definition>();
    c->length_type = d.length_type;
    c->type = d.type;
    c->extension = d.extension;
    c->references_loctr = d.references_loctr;
    c->dupl_factor = clone_expr(d.dupl_factor);
    c->program_type = clone_expr(d.program_type);
    c->length = clone_expr(d.length);
    c->scale = clone_expr(d.scale);
    c->exponent = clone_expr(d.exponent);
    c->nominal_value = clone_nominal(d.nominal_value);
    c->type_range = d.type_range;
    c->extension_range = d.extension_range;
    return c;
}

class statement_collector final : public hp::processing::statement_analyzer
{
    hp::context::hlasm_context& hlasm_ctx;
    const hp::library_info& lib_info;

public:
    std::vector<recorded_statement> statements;
    std::optional<std::string> entry;
    bool entry_unrepresentable = false;

    statement_collector(hp::context::hlasm_context& hlasm_ctx, const hp::library_info& lib_info)
        : hlasm_ctx(hlasm_ctx)
        , lib_info(lib_info)
    { }

    bool analyze(const hp::context::hlasm_statement& statement,
        hp::processing::statement_provider_kind,
        hp::processing::processing_kind proc_kind,
        bool evaluated_model) override
    {
        const auto* res = statement.access_resolved();
        if (proc_kind != hp::processing::processing_kind::ORDINARY || !res || hlasm_ctx.get_end_reached())
            return false;
        if (!evaluated_model && is_model(*res))
            return false;

        const auto& op = res->opcode_ref();
        using enum hp::context::instruction_type;
        if (op.type == MACH)
            record_machine(*res, op.instr_mach, { });
        else if (op.type == MNEMO)
            record_machine(*res, &op.instr_mnemo->instruction(), op.instr_mnemo->operand_transformations());
        else if (op.type == ASM && (op.value == hp::context::id_index("DC") || op.value == hp::context::id_index("DS")))
            record_data(*res);
        else if (op.type == ASM && op.value == hp::context::id_index("END"))
            record_entry(*res);
        return false;
    }

    void analyze_aread_line(const hlasm_plugin::utils::resource::resource_location&, size_t, std::string_view) override
    { }

private:
    static bool is_model(const hp::processing::resolved_statement& res)
    {
        using enum hp::semantics::label_si_type;
        const auto label = res.label_ref().type;
        const auto& ops = res.operands_ref().value;
        return label == CONC || label == VAR
            || (!ops.empty() && ops.front()->type == hp::semantics::operand_type::MODEL);
    }

    static std::string label_of(const hp::processing::resolved_statement& res)
    {
        const auto& label = res.label_ref();
        if (label.type != hp::semantics::label_si_type::ORD)
            return { };
        return std::string(std::get<hp::semantics::ord_symbol_string>(label.value).symbol.to_string_view());
    }

    statement_origin origin_of() const
    {
        const auto frames = hlasm_ctx.processing_stack_details();
        const auto open = std::ranges::find_if(
            frames.rbegin(), frames.rend(), [](const auto& f) { return !f.scope().is_in_macro(); });
        const auto& scopes = hlasm_ctx.scope_stack();
        const auto& innermost = scopes.back();
        return {
            std::string(open->resource_loc.get_uri()),
            open->pos.line + 1,
            innermost.is_in_macro() ? std::optional<std::string>(innermost.this_macro->id.to_string_view())
                                    : std::nullopt,
            scopes.size() - 1,
        };
    }

    void record(const hp::processing::resolved_statement& res,
        hp::context::alignment alignment,
        size_t unique_id_offset,
        std::variant<machine_body, data_body> body)
    {
        auto address = hlasm_ctx.ord_ctx.align(alignment);
        auto dep_ctx = hp::context::ordinary_assembly_dependency_solver(hlasm_ctx.ord_ctx, address, lib_info)
                           .derive_current_dependency_evaluation_context();
        dep_ctx.unique_id += unique_id_offset;
        statements.push_back({
            std::string(res.opcode_ref().value.to_string_view()),
            label_of(res),
            std::move(address),
            std::move(dep_ctx),
            origin_of(),
            std::move(body),
        });
    }

    void record_machine(const hp::processing::resolved_statement& res,
        const hp::instructions::machine_instruction* instruction,
        std::span<const hp::instructions::mnemonic_transformation> transforms)
    {
        std::vector<std::unique_ptr<hp::semantics::operand>> operands;
        for (const auto& o : res.operands_ref().value)
        {
            const auto* m = o->access_mach();
            if (!m)
                return;
            operands.push_back(std::make_unique<hp::semantics::machine_operand>(
                clone_expr(m->displacement), clone_expr(m->first_par), clone_expr(m->second_par), m->operand_range));
        }
        record(res, hp::context::halfword, 1, machine_body { instruction, transforms, std::move(operands) });
    }

    void record_data(const hp::processing::resolved_statement& res)
    {
        std::vector<std::unique_ptr<hp::expressions::data_definition>> definitions;
        for (const auto& o : res.operands_ref().value)
        {
            const auto* d = o->access_data_def();
            if (!d)
                return;
            definitions.push_back(clone_definition(*d->value));
        }
        if (definitions.empty())
            return;
        const auto alignment = definitions.front()->get_alignment();
        const bool initialised = res.opcode_ref().value == hp::context::id_index("DC");
        record(res, alignment, 1, data_body { initialised, std::move(definitions) });
    }

    void record_entry(const hp::processing::resolved_statement& res)
    {
        const auto& ops = res.operands_ref().value;
        if (ops.empty() || ops.front()->type == hp::semantics::operand_type::EMPTY)
            return;
        const auto* asm_op = ops.front()->access_asm();
        const auto* expr = asm_op ? asm_op->access_expr() : nullptr;
        const auto* symbol =
            expr ? dynamic_cast<const hp::expressions::mach_expr_symbol*>(expr->expression.get()) : nullptr;
        if (symbol)
            entry = std::string(symbol->value.to_string_view());
        else
            entry_unrepresentable = true;
    }
};

} // namespace hlasm_ast_bridge

#endif
