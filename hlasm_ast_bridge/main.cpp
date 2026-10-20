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

// hlasm_ast_bridge CLI: `--instruction-table` writes the instruction table as JSON to stdout.

#include <iostream>
#include <string_view>

#include "instruction_table_export.h"

int main(int argc, char* argv[])
{
    if (argc == 2 && std::string_view(argv[1]) == "--instruction-table")
    {
        hlasm_ast_bridge::export_instruction_table(std::cout);
        return 0;
    }
    std::cerr << "usage: hlasm_ast_bridge --instruction-table\n";
    return 2;
}
