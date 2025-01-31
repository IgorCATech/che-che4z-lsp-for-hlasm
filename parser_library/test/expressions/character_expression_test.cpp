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

#include "gtest/gtest.h"

#include "../common_testing.h"

// test for
// arithmetic SETC expressions

#define SETCEQ(X, Y)                                                                                                   \
    EXPECT_EQ(a.hlasm_ctx()                                                                                            \
                  .get_var_sym(a.hlasm_ctx().ids().add(X))                                                             \
                  ->access_set_symbol_base()                                                                           \
                  ->access_set_symbol<C_t>()                                                                           \
                  ->get_value(),                                                                                       \
        Y)

TEST(character_expresssion, operator_priority)
{
    std::string input =
        R"(
&C SETC 'ABC'.(3)'ABCDEF'(4,3)
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)0);

    SETCEQ("C", "ABCDEFDEFDEF");
}

TEST(character_expresssion, substring_notation)
{
    std::string input =
        R"(
&C1 SETC 'ABC'(1,3)
&C2 SETC '&C1'(1,2).'DEF'
&C3 SETC ''(0,0)
&C4 SETC 'XYZ'(2,*)
&C5 SETC 'XYZ'(1,0)
&C6 SETC (2)UPPER('x')
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)0);

    SETCEQ("C1", "ABC");
    SETCEQ("C2", "ABDEF");
    SETCEQ("C3", "");
    SETCEQ("C4", "YZ");
    SETCEQ("C5", "");
    SETCEQ("C6", "XX");
}

TEST(character_expresssion, invalid_substring_notation)
{
    std::string input =
        R"(
&C SETC 'ABC'(0,1)
&C SETC 'ABCDE'(7,3)
&C SETC 'ABCDE'(3,-2)
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)3);
}

/*TODO uncomment when assembler options will be implemented
TEST(character_expresssion, exceeds_warning)
{
    std::string input =
        R"(
&C SETC 'ABC'(2,3)
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)1);
    EXPECT_EQ(a.diags().front().severity, diagnostic_severity::warning);
}*/

TEST(character_expresssion, invalid_string)
{
    std::string input =
        R"(
&C SETC '&'
&C SETC (5000)'A'
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)2);
}

TEST(character_expresssion, escaping)
{
    std::string input =
        R"(
&C1 SETC 'L''SYMBOL'
&C2 SETC '&&'(1,1)
&C3 SETC 'HALF&&'
&C4 SETC '&C1..S'
&DOT SETC '.'
&C5 SETC 'A&DOT.&DOT'
&C6 SETC '&C2.A'
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)0);

    SETCEQ("C1", "L'SYMBOL");
    SETCEQ("C2", "&");
    SETCEQ("C3", "HALF&&");
    SETCEQ("C4", "L'SYMBOL.S");
    SETCEQ("C5", "A..");
    SETCEQ("C6", "&A");
}

TEST(character_expresssion, single_operand_with_spaces)
{
    std::string input =
        R"(
&C1 SETC UPPER( 'A' )
&C2 SETC UPPER( 'A')
&C3 SETC UPPER('A' )
&C4 SETC UPPER('&C1') 
&C5 SETC (UPPER '&C1')
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)0);

    SETCEQ("C1", "A");
    SETCEQ("C2", "A");
    SETCEQ("C3", "A");
    SETCEQ("C4", "A");
    SETCEQ("C5", "A");
}

TEST(character_expresssion, single_operand_fail)
{
    for (std::string input : {
             "&C SETC UPPER(&C)",
             "&C SETC (UPPER &C)",
         })
    {
        analyzer a(input);
        a.analyze();

        a.collect_diags();
        EXPECT_FALSE(a.diags().empty());
    }
}

TEST(character_expresssion, zero_length_substring)
{
    std::string input = R"(
     LCLC &EMPTY
&C1  SETC '&EMPTY'(0,0)
&C2  SETC '&EMPTY'(1,0)
&C3  SETC '&EMPTY'(2,0)
&C4  SETC 'ABCDE'(6,*)
)";
    analyzer a(input);
    a.analyze();

    a.collect_diags();
    ASSERT_EQ(a.diags().size(), (size_t)0);
}

TEST(character_expresssion, dots)
{
    for (const auto& [input, ok] : {
             std::pair<std::string, bool> { "&C SETC &C", false },
             std::pair<std::string, bool> { "&C. SETC &C", false },
             std::pair<std::string, bool> { "&C SETC &C.", false },
             std::pair<std::string, bool> { "&C. SETC &C.", false },
             std::pair<std::string, bool> { "&C SETC T'&C", true },
             std::pair<std::string, bool> { "&C SETC T'&C.", true },
         })
    {
        analyzer a(input);
        a.analyze();

        a.collect_diags();
        ASSERT_EQ(a.diags().empty(), ok);
    }
}
