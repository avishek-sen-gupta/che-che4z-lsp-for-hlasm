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

// hlasm_export CLI
//
// Usage:
//   hlasm_export <source_file> [options]
//
// Options:
//   --pretty           Pretty-print JSON output (2-space indent). Default: compact.
//   --output <file>    Write JSON to <file> instead of stdout.
//
// Reads the given HLASM source file, runs the parser-library analyzer, and
// emits the full post-analysis artifact dump as JSON.
//
// No external library resolution is performed (equivalent to assembling with
// an empty SYSLIB). To analyse code that relies on COPY members or external
// macros, extend this tool or pre-process the source.

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "analyzer.h"
#include "empty_parse_lib_provider.h"
#include "fs_parse_lib_provider.h"
#include "hlasm_json_export.h"
#include "macro_invocation_analyzer.h"
#include "utils/path_conversions.h"
#include "utils/resource_location.h"

namespace {

std::string read_file(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        throw std::runtime_error("Cannot open file: " + path);
    return { std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>() };
}

} // namespace

int main(int argc, char* argv[])
{
    std::string source_path;
    bool pretty = false;
    std::string output_path;
    std::vector<std::string> syslib_dirs;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i]);
        if (arg == "--pretty")
        {
            pretty = true;
        }
        else if (arg == "--output" && i + 1 < argc)
        {
            output_path = argv[++i];
        }
        else if (arg == "--syslib" && i + 1 < argc)
        {
            syslib_dirs.push_back(argv[++i]);
        }
        else if (arg.size() > 2 && arg.substr(0, 2) == "--")
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
        else
        {
            if (!source_path.empty())
            {
                std::cerr << "Only one source file may be specified.\n";
                return 1;
            }
            source_path = arg;
        }
    }

    if (source_path.empty())
    {
        std::cerr << "Usage: hlasm_export <source_file> [--syslib <dir>]... [--pretty] [--output <file>]\n";
        return 1;
    }

    std::string source;
    try
    {
        source = read_file(source_path);
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }

    using namespace hlasm_plugin::parser_library;
    using namespace hlasm_plugin::utils::resource;

    resource_location file_loc(hlasm_plugin::utils::path::path_to_uri(source_path));

    fs_parse_lib_provider fs_provider(syslib_dirs);
    parse_lib_provider* lib_provider = syslib_dirs.empty()
        ? static_cast<parse_lib_provider*>(&empty_parse_lib_provider::instance)
        : static_cast<parse_lib_provider*>(&fs_provider);

    macro_invocation_analyzer inv_analyzer;

    analyzer a(source,
        analyzer_options { file_loc, lib_provider, file_is_opencode::yes });
    a.register_stmt_analyzer(&inv_analyzer);
    a.analyze();

    nlohmann::json result = hlasm_json_export::export_to_json(a, &inv_analyzer.invocations);

    std::string json_text = pretty ? result.dump(2) : result.dump();

    if (output_path.empty())
    {
        std::cout << json_text << "\n";
    }
    else
    {
        std::ofstream ofs(output_path);
        if (!ofs)
        {
            std::cerr << "Cannot write to: " << output_path << "\n";
            return 1;
        }
        ofs << json_text << "\n";
    }

    return 0;
}
