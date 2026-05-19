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

#include "common_testing.h"
#include "mock_parse_lib_provider.h"
#include "compiler_options.h"
#include "../hlasm_json_export.h"

// Experiment 3: Export all post-analysis artifacts (symbols, sections,
// global SET variables, macros, copy members, diagnostics, occurrences)
// as a nlohmann::json object and assert on the JSON structure.
//
// The BMS source from Experiment 2 is reused as the input because it
// exercises every data category: DSECT sections, ordinary symbols,
// global SETC variables, macros, and a COPY-less open-code file.

TEST(experiment_json_export, bms_full_export)
{
    static const std::string dfhmsd_src = " MACRO\n"
        "&NAME    DFHMSD &TYPE=,&MODE=,&LANG=,&STORAGE=,&CTRL=,                 X\n"
        "               &TIOAPFX=,&MAPATTS=,&DSATTS=,&TERM=,&BASE=\n"
        "         GBLC  &BMS_TYPE,&BMS_MODE,&BMS_LANG\n"
        "         AIF   ('&TYPE' EQ 'FINAL').DONE\n"
        "&BMS_TYPE SETC '&TYPE'\n"
        "&BMS_MODE SETC '&MODE'\n"
        "&BMS_LANG SETC '&LANG'\n"
        "         AIF   ('&NAME' EQ '').DONE\n"
        "&NAME    DSECT\n"
        ".DONE    ANOP\n"
        "         MEND\n";

    static const std::string dfhmdi_src = " MACRO\n"
        "&NAME    DFHMDI &SIZE=,&LINE=,&COLUMN=\n"
        "         GBLC  &BMS_MAP_SIZE\n"
        "&BMS_MAP_SIZE SETC '&SIZE'\n"
        "&NAME    DS    0H\n"
        "         MEND\n";

    static const std::string dfhmdf_src = " MACRO\n"
        "&NAME    DFHMDF &POS=,&LEN=,&ATTRB=,&COLOR=,&INITIAL=\n"
        "         GBLC  &BMS_FLD_POS,&BMS_FLD_LEN\n"
        "&BMS_FLD_POS  SETC '&POS'\n"
        "&BMS_FLD_LEN  SETC '&LEN'\n"
        "         AIF   ('&NAME' EQ '').DONE\n"
        "         AIF   ('&LEN' EQ '').DONE\n"
        "&NAME    DS    CL&LEN\n"
        ".DONE    ANOP\n"
        "         MEND\n";

    mock_parse_lib_provider libs {
        { "DFHMSD", dfhmsd_src },
        { "DFHMDI", dfhmdi_src },
        { "DFHMDF", dfhmdf_src },
    };

    std::string bms_src =
        "\n"
        "ORDRMAP  DFHMSD TYPE=DSECT,                                            X\n"
        "               MODE=INOUT,                                             X\n"
        "               LANG=COBOL,                                             X\n"
        "               STORAGE=AUTO,                                           X\n"
        "               CTRL=(FREEKB,FRSET),                                    X\n"
        "               TIOAPFX=YES,                                            X\n"
        "               TERM=3270-2\n"
        "*\n"
        "ORDMAP1  DFHMDI SIZE=(24,80),                                          X\n"
        "               LINE=1,                                                 X\n"
        "               COLUMN=1\n"
        "*\n"
        "ORDNAME  DFHMDF POS=(3,1),                                             X\n"
        "               LEN=20,                                                 X\n"
        "               ATTRB=(ASKIP,NORM),                                     X\n"
        "               COLOR=NEUTRAL\n"
        "*\n"
        "         DFHMSD TYPE=FINAL\n";

    analyzer a(bms_src, analyzer_options { &libs, file_is_opencode::yes });
    a.analyze();

    ASSERT_TRUE(a.diags().empty());

    // --- export ---
    nlohmann::json j = hlasm_json_export::export_to_json(a);

    // Basic structure
    ASSERT_TRUE(j.contains("symbols"));
    ASSERT_TRUE(j.contains("sections"));
    ASSERT_TRUE(j.contains("global_variables"));
    ASSERT_TRUE(j.contains("macros"));
    ASSERT_TRUE(j.contains("copy_members"));
    ASSERT_TRUE(j.contains("diagnostics"));
    ASSERT_TRUE(j.contains("occurrences"));

    // --- sections ---
    // Find ORDRMAP section with kind DUMMY
    {
        bool found = false;
        for (const auto& sec : j["sections"])
        {
            if (sec["name"] == "ORDRMAP" && sec["kind"] == "DUMMY")
            {
                found = true;
                break;
            }
        }
        EXPECT_TRUE(found) << "ORDRMAP DUMMY section not found in JSON";
    }

    // --- symbols ---
    // ORDMAP1 and ORDNAME must appear as symbols
    {
        bool has_ordmap1 = false;
        bool has_ordname = false;
        for (const auto& sym : j["symbols"])
        {
            if (sym["name"] == "ORDMAP1")
                has_ordmap1 = true;
            if (sym["name"] == "ORDNAME")
                has_ordname = true;
        }
        EXPECT_TRUE(has_ordmap1) << "ORDMAP1 not found in symbols";
        EXPECT_TRUE(has_ordname) << "ORDNAME not found in symbols";
    }

    // --- global_variables ---
    ASSERT_TRUE(j["global_variables"].contains("BMS_MODE"));
    EXPECT_EQ(j["global_variables"]["BMS_MODE"]["value"], "INOUT");
    EXPECT_EQ(j["global_variables"]["BMS_MODE"]["type"], "C");

    ASSERT_TRUE(j["global_variables"].contains("BMS_LANG"));
    EXPECT_EQ(j["global_variables"]["BMS_LANG"]["value"], "COBOL");

    ASSERT_TRUE(j["global_variables"].contains("BMS_MAP_SIZE"));
    EXPECT_EQ(j["global_variables"]["BMS_MAP_SIZE"]["value"], "(24,80)");

    ASSERT_TRUE(j["global_variables"].contains("BMS_FLD_POS"));
    EXPECT_EQ(j["global_variables"]["BMS_FLD_POS"]["value"], "(3,1)");

    ASSERT_TRUE(j["global_variables"].contains("BMS_FLD_LEN"));
    EXPECT_EQ(j["global_variables"]["BMS_FLD_LEN"]["value"], "20");

    // --- macros ---
    {
        bool has_dfhmsd = false;
        bool has_dfhmdi = false;
        bool has_dfhmdf = false;
        for (const auto& m : j["macros"])
        {
            if (m == "DFHMSD")
                has_dfhmsd = true;
            if (m == "DFHMDI")
                has_dfhmdi = true;
            if (m == "DFHMDF")
                has_dfhmdf = true;
        }
        EXPECT_TRUE(has_dfhmsd);
        EXPECT_TRUE(has_dfhmdi);
        EXPECT_TRUE(has_dfhmdf);
    }

    // --- no diagnostics in export ---
    EXPECT_TRUE(j["diagnostics"].empty());

    // --- occurrences: at least some ORD occurrences should exist ---
    {
        bool has_ord_occurrence = false;
        for (const auto& occ : j["occurrences"])
        {
            if (occ["kind"] == "ORD")
            {
                has_ord_occurrence = true;
                // Each occurrence has name and range with start/end
                EXPECT_TRUE(occ.contains("name"));
                EXPECT_TRUE(occ.contains("range"));
                EXPECT_TRUE(occ["range"].contains("start"));
                EXPECT_TRUE(occ["range"].contains("end"));
                break;
            }
        }
        EXPECT_TRUE(has_ord_occurrence) << "No ORD occurrences found";
    }
}
