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

#ifndef HLASM_JSON_EXPORT_H
#define HLASM_JSON_EXPORT_H

// Header-only utility: export HLASM post-analysis artifacts as a nlohmann::json object.
//
// Covers:
//   - ordinary symbols (name, value_kind, abs_value, section)
//   - sections (name, kind)
//   - global SET variables (name, type, scalar value)
//   - macros (name)
//   - copy members (name)
//   - diagnostics (severity, code, message, range)
//   - LSP occurrences for the opencode file (kind, name, range)

#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include "analyzer.h"
#include "analyzing_context.h"
#include "macro_invocation_analyzer.h"
#include "context/hlasm_context.h"
#include "context/ordinary_assembly/ordinary_assembly_context.h"
#include "context/ordinary_assembly/section.h"
#include "context/ordinary_assembly/symbol.h"
#include "context/ordinary_assembly/symbol_value.h"
#include "context/variables/set_symbol.h"
#include "diagnostic.h"
#include "lsp/lsp_context.h"
#include "lsp/symbol_occurrence.h"

namespace hlasm_json_export {

namespace detail {

using namespace hlasm_plugin::parser_library;
using namespace hlasm_plugin::parser_library::context;
using namespace hlasm_plugin::parser_library::lsp;

inline std::string section_kind_str(section_kind k)
{
    switch (k)
    {
        case section_kind::DUMMY:
            return "DUMMY";
        case section_kind::COMMON:
            return "COMMON";
        case section_kind::EXECUTABLE:
            return "EXECUTABLE";
        case section_kind::READONLY:
            return "READONLY";
        case section_kind::EXTERNAL:
            return "EXTERNAL";
        case section_kind::WEAK_EXTERNAL:
            return "WEAK_EXTERNAL";
        case section_kind::EXTERNAL_DSECT:
            return "EXTERNAL_DSECT";
    }
    return "UNKNOWN";
}

inline std::string occurrence_kind_str(occurrence_kind k)
{
    switch (k)
    {
        case occurrence_kind::ORD:
            return "ORD";
        case occurrence_kind::VAR:
            return "VAR";
        case occurrence_kind::SEQ:
            return "SEQ";
        case occurrence_kind::INSTR:
            return "INSTR";
        case occurrence_kind::INSTR_LIKE:
            return "INSTR_LIKE";
        case occurrence_kind::COPY_OP:
            return "COPY_OP";
    }
    return "UNKNOWN";
}

inline nlohmann::json range_to_json(const range& r)
{
    return { { "start", { { "line", r.start.line }, { "column", r.start.column } } },
        { "end", { { "line", r.end.line }, { "column", r.end.column } } } };
}

inline nlohmann::json export_symbols(const ordinary_assembly_context& ord)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [idx, var] : ord.symbols())
    {
        if (var.index() != 0) // skip using_label_tag and macro_label_tag
            continue;
        const auto& sym = std::get<symbol>(var);
        nlohmann::json entry;
        entry["name"] = std::string(idx.to_string_view());
        switch (sym.kind())
        {
            case symbol_value_kind::ABS:
                entry["value_kind"] = "ABS";
                entry["abs_value"] = sym.value().get_abs();
                break;
            case symbol_value_kind::RELOC:
                entry["value_kind"] = "RELOC";
                break;
            case symbol_value_kind::UNDEF:
                entry["value_kind"] = "UNDEF";
                break;
        }
        arr.push_back(std::move(entry));
    }
    return arr;
}

inline nlohmann::json export_sections(const ordinary_assembly_context& ord)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& sec_ptr : ord.sections())
    {
        nlohmann::json entry;
        entry["name"] = std::string(sec_ptr->name.to_string_view());
        entry["kind"] = section_kind_str(sec_ptr->kind);
        arr.push_back(std::move(entry));
    }
    return arr;
}

inline nlohmann::json export_globals(const hlasm_context& ctx)
{
    nlohmann::json obj = nlohmann::json::object();
    for (const auto& [idx, var] : ctx.globals())
    {
        std::string name(idx.to_string_view());
        std::visit(
            [&](const auto& sym) {
                using T = std::decay_t<decltype(sym)>;
                nlohmann::json entry;
                entry["is_scalar"] = sym.is_scalar;
                if constexpr (std::is_same_v<T, set_symbol<A_t>>)
                {
                    entry["type"] = "A";
                    if (sym.is_scalar)
                        entry["value"] = sym.get_value();
                    else
                    {
                        nlohmann::json arr = nlohmann::json::array();
                        for (auto k : sym.keys())
                            arr.push_back({ { "index", k }, { "value", sym.get_value(k) } });
                        entry["value"] = std::move(arr);
                    }
                }
                else if constexpr (std::is_same_v<T, set_symbol<B_t>>)
                {
                    entry["type"] = "B";
                    if (sym.is_scalar)
                        entry["value"] = sym.get_value();
                }
                else if constexpr (std::is_same_v<T, set_symbol<C_t>>)
                {
                    entry["type"] = "C";
                    if (sym.is_scalar)
                        entry["value"] = sym.get_value();
                    else
                    {
                        nlohmann::json arr = nlohmann::json::array();
                        for (auto k : sym.keys())
                            arr.push_back({ { "index", k }, { "value", sym.get_value(k) } });
                        entry["value"] = std::move(arr);
                    }
                }
                obj[name] = std::move(entry);
            },
            var);
    }
    return obj;
}

inline nlohmann::json export_macros(const hlasm_context& ctx)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [idx, _] : ctx.macros())
        arr.push_back(std::string(idx.to_string_view()));
    return arr;
}

inline nlohmann::json export_copy_members(hlasm_context& ctx)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& [idx, _] : ctx.copy_members())
        arr.push_back(std::string(idx.to_string_view()));
    return arr;
}

inline nlohmann::json export_diagnostics(const analyzer& a)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& d : a.diags())
    {
        nlohmann::json entry;
        entry["severity"] = static_cast<int>(d.severity);
        entry["code"] = d.code;
        entry["message"] = d.message;
        entry["range"] = range_to_json(d.diag_range);
        arr.push_back(std::move(entry));
    }
    return arr;
}

inline nlohmann::json export_occurrences(const analyzer& a)
{
    auto ctx = a.context();
    if (!ctx.lsp_ctx)
        return nlohmann::json::array();

    // Get file info for the opencode file (empty URI when constructed with plain string)
    const auto* fi = ctx.lsp_ctx->get_file_info(
        hlasm_plugin::utils::resource::resource_location(""));
    if (!fi)
        return nlohmann::json::array();

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& occ : fi->get_occurrences())
    {
        nlohmann::json entry;
        entry["kind"] = occurrence_kind_str(occ.kind);
        entry["name"] = std::string(occ.name.to_string_view());
        entry["range"] = range_to_json(occ.occurrence_range);
        arr.push_back(std::move(entry));
    }
    return arr;
}

} // namespace detail

namespace detail {

inline nlohmann::json export_macro_invocations(
    const std::vector<hlasm_plugin::parser_library::macro_invocation_record>& invocations)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& inv : invocations)
    {
        nlohmann::json entry;
        entry["name"] = inv.macro_name;
        if (!inv.label.empty())
            entry["label"] = inv.label;
        entry["range"] = range_to_json(inv.invocation_range);
        nlohmann::json args = nlohmann::json::object();
        for (const auto& [k, v] : inv.keyword_args)
            args[k] = v;
        entry["args"] = std::move(args);
        arr.push_back(std::move(entry));
    }
    return arr;
}

} // namespace detail

// Main export function: returns a JSON object with all post-analysis artifacts.
inline nlohmann::json export_to_json(hlasm_plugin::parser_library::analyzer& a,
    const std::vector<hlasm_plugin::parser_library::macro_invocation_record>* invocations = nullptr)
{
    using namespace detail;
    auto& ctx = a.hlasm_ctx();
    const auto& ord = ctx.ord_ctx;

    nlohmann::json result;
    result["symbols"] = export_symbols(ord);
    result["sections"] = export_sections(ord);
    result["global_variables"] = export_globals(ctx);
    result["macros"] = export_macros(ctx);
    result["copy_members"] = export_copy_members(ctx);
    result["diagnostics"] = export_diagnostics(a);
    result["occurrences"] = export_occurrences(a);
    if (invocations)
        result["macro_invocations"] = export_macro_invocations(*invocations);
    return result;
}

} // namespace hlasm_json_export

#endif // HLASM_JSON_EXPORT_H
