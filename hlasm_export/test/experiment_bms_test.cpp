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
#include "context/hlasm_context.h"
#include "context/ordinary_assembly/ordinary_assembly_context.h"
#include "context/ordinary_assembly/section.h"

// Experiment 2: BMS-like source processed through stub macros that capture
// directive parameters into global SETC variables.
//
// Key pattern: inside each macro body, global SETC variables named
//   &BMS_<PARAM>
// are declared and assigned the incoming keyword argument. After analysis,
// get_global_var_value<C_t>() reads them back as strings.
//
// Note: continuation marker X must be in column 72 (1-indexed).

TEST(experiment_bms, param_capture_via_global_setc)
{
    // DFHMSD stub: captures TYPE/MODE/LANG; creates DSECT for named mapsets.
    // Guard: when TYPE=FINAL, skip SETC assignments and DSECT creation.
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

    // DFHMDI stub: captures SIZE per map name; creates DS 0H label
    static const std::string dfhmdi_src = " MACRO\n"
        "&NAME    DFHMDI &SIZE=,&LINE=,&COLUMN=\n"
        "         GBLC  &BMS_MAP_SIZE\n"
        "&BMS_MAP_SIZE SETC '&SIZE'\n"
        "&NAME    DS    0H\n"
        "         MEND\n";

    // DFHMDF stub: captures POS/LEN per field name; creates DS CL<len>.
    // Uses LEN= (not LENGTH=) to avoid any keyword conflicts.
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

    // BMS source: continuation X must be in column 72.
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

    EXPECT_TRUE(a.diags().empty());

    auto& ctx = a.hlasm_ctx();

    // DSECT for mapset was created (DFHMSD with TYPE=DSECT + label ORDRMAP)
    const auto* ordrmap_sec = get_section(ctx, "ORDRMAP");
    ASSERT_NE(ordrmap_sec, nullptr);
    EXPECT_EQ(ordrmap_sec->kind, context::section_kind::DUMMY);

    // Labels inside DSECT were defined
    EXPECT_NE(get_symbol(ctx, "ORDMAP1"), nullptr);
    EXPECT_NE(get_symbol(ctx, "ORDNAME"), nullptr);

    // Global SETC variables captured from DFHMSD call (FINAL guard keeps them)
    auto bms_mode = get_global_var_value<C_t>(ctx, "BMS_MODE");
    ASSERT_TRUE(bms_mode.has_value());
    EXPECT_EQ(*bms_mode, "INOUT");

    auto bms_lang = get_global_var_value<C_t>(ctx, "BMS_LANG");
    ASSERT_TRUE(bms_lang.has_value());
    EXPECT_EQ(*bms_lang, "COBOL");

    // Global SETC variables from DFHMDI call
    auto bms_map_size = get_global_var_value<C_t>(ctx, "BMS_MAP_SIZE");
    ASSERT_TRUE(bms_map_size.has_value());
    EXPECT_EQ(*bms_map_size, "(24,80)");

    // Global SETC variables from DFHMDF call
    auto bms_fld_pos = get_global_var_value<C_t>(ctx, "BMS_FLD_POS");
    ASSERT_TRUE(bms_fld_pos.has_value());
    EXPECT_EQ(*bms_fld_pos, "(3,1)");

    auto bms_fld_len = get_global_var_value<C_t>(ctx, "BMS_FLD_LEN");
    ASSERT_TRUE(bms_fld_len.has_value());
    EXPECT_EQ(*bms_fld_len, "20");
}
