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

#ifndef HLASM_EXPORT_FS_PARSE_LIB_PROVIDER_H
#define HLASM_EXPORT_FS_PARSE_LIB_PROVIDER_H

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "analyzer.h"
#include "parse_lib_provider.h"
#include "utils/path_conversions.h"
#include "utils/resource_location.h"

namespace hlasm_plugin::parser_library {

class fs_parse_lib_provider : public parse_lib_provider
{
    std::vector<std::filesystem::path> m_dirs;

    static constexpr std::string_view extensions[] = { "", ".mac", ".hlasm", ".asm" };

    std::optional<std::pair<std::string, std::filesystem::path>> find_file(std::string_view name) const
    {
        for (const auto& dir : m_dirs)
        {
            for (auto ext : extensions)
            {
                std::filesystem::path candidate = dir / (std::string(name) + std::string(ext));
                std::ifstream ifs(candidate, std::ios::binary);
                if (!ifs)
                    continue;
                std::string content { std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>() };
                return std::pair { std::move(content), std::move(candidate) };
            }
        }
        return std::nullopt;
    }

public:
    explicit fs_parse_lib_provider(std::vector<std::string> dirs)
    {
        m_dirs.reserve(dirs.size());
        for (auto& d : dirs)
            m_dirs.emplace_back(std::move(d));
    }

    utils::value_task<bool> parse_library(
        std::string library, analyzing_context ctx, processing::processing_kind kind) override
    {
        auto found = find_file(library);
        if (!found)
            co_return false;

        auto& [content, path] = *found;
        auto uri = hlasm_plugin::utils::path::path_to_uri(path.string());

        auto a = std::make_unique<analyzer>(content,
            analyzer_options {
                utils::resource::resource_location(uri),
                this,
                std::move(ctx),
                analyzer_options::dependency(library, kind),
            });
        co_await a->co_analyze();

        co_return true;
    }

    bool has_library(std::string_view library, utils::resource::resource_location* loc) override
    {
        auto found = find_file(library);
        if (!found)
            return false;

        if (loc)
            *loc = utils::resource::resource_location(
                hlasm_plugin::utils::path::path_to_uri(found->second.string()));
        return true;
    }

    utils::value_task<std::optional<std::pair<std::string, utils::resource::resource_location>>> get_library(
        std::string library) override
    {
        auto found = find_file(library);
        if (!found)
            co_return std::nullopt;

        auto uri = hlasm_plugin::utils::path::path_to_uri(found->second.string());
        co_return std::pair { std::move(found->first), utils::resource::resource_location(std::move(uri)) };
    }
};

} // namespace hlasm_plugin::parser_library

#endif // HLASM_EXPORT_FS_PARSE_LIB_PROVIDER_H
