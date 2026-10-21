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

// hlasm_ast_bridge CLI: `--instruction-table` writes the instruction table, `<src> [--source-name name] [--syslib dir]...` the program.

#include <iostream>
#include <string>
#include <vector>

#include "program_export.h"

int main(int argc, char* argv[])
{
    const std::vector<std::string> args(argv + 1, argv + argc);
    return hlasm_ast_bridge::run(args, std::cout, std::cerr);
}
