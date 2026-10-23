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

#ifndef HLASMPARSER_PARSERLIBRARY_COMPILER_OPTIONS_H
#define HLASMPARSER_PARSERLIBRARY_COMPILER_OPTIONS_H

#include <optional>
#include <string>

#include "instruction_set_version.h"
#include "utils/time.h"

// This file contains assembler compiler options definitions.

namespace hlasm_plugin::parser_library {
// The one instant an assembly reads its time from: local for &SYSDATE, &SYSDATC, &SYSTIME and AREAD, UTC for &SYSCLOCK.
struct assembly_clock
{
    utils::timestamp local;
    utils::timestamp utc;

    bool operator==(const assembly_clock&) const = default;
};

struct asm_option
{
    std::optional<std::string> sysparm = "";
    std::string profile;

    static constexpr instruction_set_version instr_set_default = instruction_set_version::UNI;
    instruction_set_version instr_set = instr_set_default;

    static const std::string system_id_default;
    std::string system_id = system_id_default;

    static constexpr unsigned int sysopt_rent_default = 0;
    unsigned int sysopt_rent = sysopt_rent_default;

    static constexpr bool sysopt_xobject_default = false;
    bool sysopt_xobject = sysopt_xobject_default;

    long long statement_count_limit = 10'000'000;

    std::optional<std::string> sysin_dsn = "";
    std::optional<std::string> sysin_member = "";
    std::optional<std::string> sysjob;
    std::optional<std::string> sysstep;
    std::optional<assembly_clock> clock;

    bool operator==(const asm_option&) const = default;
};
} // namespace hlasm_plugin::parser_library
#endif
