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

#ifndef HLASM_EXPORT_MACRO_INVOCATION_ANALYZER_H
#define HLASM_EXPORT_MACRO_INVOCATION_ANALYZER_H

#include <map>
#include <string>
#include <vector>

#include "context/instruction_type.h"
#include "processing/statement.h"
#include "processing/statement_analyzers/statement_analyzer.h"
#include "semantics/concatenation.h"
#include "semantics/operand_impls.h"
#include "semantics/statement_fields.h"

namespace hlasm_plugin::parser_library {

struct macro_invocation_record
{
    std::string macro_name;
    std::string label;
    std::map<std::string, std::string> keyword_args;
    range invocation_range;
};

class macro_invocation_analyzer : public processing::statement_analyzer
{
public:
    std::vector<macro_invocation_record> invocations;

    bool analyze(const context::hlasm_statement& statement,
        processing::statement_provider_kind,
        processing::processing_kind,
        bool) override
    {
        const auto* res = statement.access_resolved();
        if (!res)
            return false;

        if (res->opcode_ref().type != context::instruction_type::MAC)
            return false;

        macro_invocation_record record;
        record.macro_name = std::string(res->opcode_ref().value.to_string_view());
        record.invocation_range = res->stmt_range_ref();

        // Capture label if present.
        //
        // The name field of a macro invocation is not required to be an ordinary
        // symbol, and HLASM says so with two label kinds: ORD when it parses as
        // one, MAC for any other name-field text. Both are names here.
        //
        // Reading only ORD loses exactly the names a generator cares about most.
        // BMS field names follow the rules of the generated language, so under
        // LANG=COBOL a DFHMDF may legitimately be called SC-ELIG -- not an
        // ordinary symbol, since HLASM symbols admit no hyphen, so it arrives as
        // MAC. Dropping it leaves the invocation indistinguishable from a
        // genuinely unnamed constant field, and every symbolic name BMS derives
        // from it disappears from the generated copybook with no diagnostic
        // anywhere.
        const auto& lbl = res->label_ref();
        if (lbl.type == semantics::label_si_type::ORD)
            record.label = std::get<semantics::ord_symbol_string>(lbl.value).mixed_case;
        else if (lbl.type == semantics::label_si_type::MAC)
            record.label = std::get<std::string>(lbl.value);

        // Capture keyword arguments
        for (const auto& op : res->operands_ref().value)
        {
            const auto* mac_op = op->access_mac();
            if (!mac_op)
                continue;

            const auto& chain = mac_op->chain;
            if (chain.size() < 2)
                continue;
            if (!std::holds_alternative<semantics::char_str_conc>(chain[0].value))
                continue;
            if (!std::holds_alternative<semantics::equals_conc>(chain[1].value))
                continue;

            std::string kwd_name = std::get<semantics::char_str_conc>(chain[0].value).value;
            std::string kwd_value =
                semantics::concatenation_point::to_string(chain.begin() + 2, chain.end());

            record.keyword_args.emplace(std::move(kwd_name), std::move(kwd_value));
        }

        invocations.push_back(std::move(record));
        return false;
    }

    void analyze_aread_line(
        const utils::resource::resource_location&, size_t, std::string_view) override
    {}
};

} // namespace hlasm_plugin::parser_library

#endif // HLASM_EXPORT_MACRO_INVOCATION_ANALYZER_H
