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

#include "ca_symbol_attribute.h"

#include "ca_var_sym.h"
#include "context/ordinary_assembly/dependable.h"
#include "context/ordinary_assembly/ordinary_assembly_dependency_solver.h"
#include "ebcdic_encoding.h"
#include "expressions/conditional_assembly/ca_expr_visitor.h"
#include "expressions/evaluation_context.h"
#include "lexing/lexer.h"
#include "semantics/statement_fields.h"

namespace hlasm_plugin::parser_library::expressions {

context::SET_t_enum get_attribute_type(context::data_attr_kind attr)
{
    switch (attr)
    {
        case context::data_attr_kind::T:
        case context::data_attr_kind::O:
            return context::SET_t_enum::C_TYPE;
        case context::data_attr_kind::L:
        case context::data_attr_kind::S:
        case context::data_attr_kind::I:
        case context::data_attr_kind::K:
        case context::data_attr_kind::N:
        case context::data_attr_kind::D:
            return context::SET_t_enum::A_TYPE;
        default:
            return context::SET_t_enum::UNDEF_TYPE;
    }
}

ca_symbol_attribute::ca_symbol_attribute(
    context::id_index symbol, context::data_attr_kind attribute, range expr_range, range symbol_rng)
    : ca_expression(get_attribute_type(attribute), std::move(expr_range))
    , attribute(attribute)
    , symbol(symbol)
    , symbol_range(symbol_rng)
{}

ca_symbol_attribute::ca_symbol_attribute(
    semantics::vs_ptr symbol, context::data_attr_kind attribute, range expr_range, range symbol_rng)
    : ca_expression(get_attribute_type(attribute), std::move(expr_range))
    , attribute(attribute)
    , symbol(std::move(symbol))
    , symbol_range(symbol_rng)
{}

ca_symbol_attribute::ca_symbol_attribute(
    semantics::literal_si lit, context::data_attr_kind attribute, range expr_range, range symbol_rng)
    : ca_expression(get_attribute_type(attribute), std::move(expr_range))
    , attribute(attribute)
    , symbol(std::move(lit))
    , symbol_range(symbol_rng)
{}

undef_sym_set ca_symbol_attribute::get_undefined_attributed_symbols(const evaluation_context& eval_ctx) const
{
    if (std::holds_alternative<context::id_index>(symbol))
    {
        if (context::symbol_attributes::is_ordinary_attribute(attribute)
            && !eval_ctx.hlasm_ctx.ord_ctx.get_symbol(std::get<context::id_index>(symbol))
            && !eval_ctx.hlasm_ctx.ord_ctx.get_symbol_reference(std::get<context::id_index>(symbol)))
            return { std::get<context::id_index>(symbol) };
        return undef_sym_set();
    }
    else if (std::holds_alternative<semantics::vs_ptr>(symbol))
    {
        const auto& vs = std::get<semantics::vs_ptr>(symbol);

        auto undef_syms = ca_var_sym::get_undefined_attributed_symbols_vs(vs, eval_ctx);

        if (undef_syms.empty() && context::symbol_attributes::is_ordinary_attribute(attribute))
        {
            context::SET_t substituted_name = vs->evaluate(eval_ctx);

            if (substituted_name.type != context::SET_t_enum::C_TYPE)
                return {};

            auto [valid, ord_name] =
                eval_ctx.hlasm_ctx.try_get_symbol_name(try_extract_leading_symbol(substituted_name.access_c()));

            if (!valid)
                return {};

            if (context::symbol_attributes::is_ordinary_attribute(attribute)
                && !eval_ctx.hlasm_ctx.ord_ctx.get_symbol(ord_name)
                && !eval_ctx.hlasm_ctx.ord_ctx.get_symbol_reference(ord_name))
                return { ord_name };
        }
        return undef_syms;
    }
    else if (std::holds_alternative<semantics::literal_si>(symbol))
    {
        // everything needs to be defined
        return undef_sym_set();
    }
    else
    {
        assert(false);
        return undef_sym_set();
    }
}

void ca_symbol_attribute::resolve_expression_tree(context::SET_t_enum kind, diagnostic_op_consumer& diags)
{
    if (kind == context::SET_t_enum::C_TYPE && kind != expr_kind)
        diags.add_diagnostic(diagnostic_op::error_CE004(expr_range));
    else if (std::holds_alternative<semantics::vs_ptr>(symbol))
        ca_var_sym::resolve_expression_tree_vs(std::get<semantics::vs_ptr>(symbol));
}

bool ca_symbol_attribute::is_character_expression(character_expression_purpose) const
{
    return get_attribute_type(attribute) == context::SET_t_enum::C_TYPE;
}

void ca_symbol_attribute::apply(ca_expr_visitor& visitor) const { visitor.visit(*this); }

context::SET_t ca_symbol_attribute::evaluate(const evaluation_context& eval_ctx) const
{
    if (std::holds_alternative<context::id_index>(symbol))
    {
        return evaluate_ordsym(std::get<context::id_index>(symbol), eval_ctx);
    }

    if (std::holds_alternative<semantics::vs_ptr>(symbol))
    {
        return evaluate_varsym(std::get<semantics::vs_ptr>(symbol), eval_ctx);
    }

    if (std::holds_alternative<semantics::literal_si>(symbol))
    {
        return evaluate_literal(std::get<semantics::literal_si>(symbol), eval_ctx);
    }

    return context::SET_t(expr_kind);
}
std::string ca_symbol_attribute::try_extract_leading_symbol(std::string_view expr)
{
    // remove parentheses
    while (!expr.empty() && expr.front() == '(' && expr.back() == ')')
    {
        expr.remove_prefix(1);
        expr.remove_suffix(1);
    }

    // remove leading using prefixes
    for (auto p = expr.find_first_of('.'); p != std::string_view::npos && !std::isdigit((unsigned char)expr.front())
         && std::all_of(expr.begin(), expr.begin() + p, lexing::lexer::ord_char);
         p = expr.find_first_of('.'))
        expr.remove_prefix(p + 1);

    // try to isolate one ordinary symbol
    if (!expr.empty() && !std::isdigit((unsigned char)expr.front()) && lexing::lexer::ord_char(expr.front()))
    {
        if (auto d = expr.find_first_of("+-*/()"); d != std::string_view::npos)
            expr = expr.substr(0, d);
    }
    return std::string(expr);
}

context::SET_t ca_symbol_attribute::get_ordsym_attr_value(
    context::id_index name, const evaluation_context& eval_ctx) const
{
    const context::symbol* ord_symbol = eval_ctx.hlasm_ctx.ord_ctx.get_symbol(name);

    if (!ord_symbol)
        ord_symbol = eval_ctx.hlasm_ctx.ord_ctx.get_symbol_reference(name);

    return retrieve_value(ord_symbol, eval_ctx);
}

context::SET_t ca_symbol_attribute::retrieve_value(
    const context::symbol* ord_symbol, const evaluation_context& eval_ctx) const
{
    if (attribute == context::data_attr_kind::T)
        return eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, ord_symbol);

    if (!ord_symbol)
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::warning_W013(expr_range));
        return context::symbol_attributes::default_value(attribute);
    }

    if ((attribute == context::data_attr_kind::S || attribute == context::data_attr_kind::I)
        && !ord_symbol->attributes().can_have_SI_attr())
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::error_E066(expr_range));
        return context::symbol_attributes::default_value(attribute);
    }

    if (!ord_symbol->attributes().is_defined(attribute))
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::warning_W013(expr_range));
        return context::symbol_attributes::default_value(attribute);
    }

    return eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, ord_symbol);
}

context::SET_t ca_symbol_attribute::evaluate_ordsym(context::id_index name, const evaluation_context& eval_ctx) const
{
    if (context::symbol_attributes::is_ordinary_attribute(attribute))
    {
        return get_ordsym_attr_value(name, eval_ctx);
    }
    else if (attribute == context::data_attr_kind::D)
    {
        return eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, name);
    }
    else if (attribute == context::data_attr_kind::O)
    {
        auto tmp = eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, name);
        if (tmp.access_c() == "U" && eval_ctx.lib_provider.has_library(*name, eval_ctx.hlasm_ctx.opencode_file_name()))
            return std::string("S");
        return tmp;
    }
    else
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::error_E066(expr_range));
        return context::symbol_attributes::default_ca_value(attribute);
    }
}

context::SET_t ca_symbol_attribute::evaluate_literal(
    const semantics::literal_si& lit, const evaluation_context& eval_ctx) const
{
    context::ordinary_assembly_dependency_solver solver(
        eval_ctx.hlasm_ctx.ord_ctx, eval_ctx.hlasm_ctx.ord_ctx.align(context::no_align));

    if (attribute == context::data_attr_kind::D)
        return false;
    else if (attribute == context::data_attr_kind::O)
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::error_E066(expr_range));
        return {};
    }
    else if (attribute == context::data_attr_kind::T)
    {
        return "U";
    }
    else
    {
        context::symbol_attributes attrs(context::symbol_origin::DAT,
            ebcdic_encoding::a2e[(unsigned char)lit->get_dd().get_type_attribute()],
            lit->get_dd().get_length_attribute(solver, eval_ctx.diags),
            lit->get_dd().get_scale_attribute(solver, eval_ctx.diags),
            lit->get_dd().get_integer_attribute(solver, eval_ctx.diags));
        if ((attribute == context::data_attr_kind::S || attribute == context::data_attr_kind::I)
            && !attrs.can_have_SI_attr())
        {
            eval_ctx.diags.add_diagnostic(diagnostic_op::warning_W011(symbol_range));
            return 0;
        }
        return attrs.get_attribute_value(attribute);
    }
}

std::vector<size_t> transform(const std::vector<context::A_t>& v)
{
    std::vector<size_t> ret;
    for (auto val : v)
        ret.push_back((size_t)val);
    return ret;
}

context::SET_t ca_symbol_attribute::evaluate_varsym(
    const semantics::vs_ptr& vs, const evaluation_context& eval_ctx) const
{
    auto [var_name, expr_subscript] = vs->evaluate_symbol(eval_ctx);

    // get symbol
    auto var_symbol = eval_ctx.hlasm_ctx.get_var_sym(var_name);

    if (!var_symbol)
    {
        eval_ctx.diags.add_diagnostic(diagnostic_op::error_E010("variable", vs->symbol_range));
        return context::symbol_attributes::default_ca_value(attribute);
    }

    // must substitute var sym
    if (context::symbol_attributes::requires_ordinary_symbol(attribute))
    {
        return evaluate_substituted(var_name, std::move(expr_subscript), vs->symbol_range, eval_ctx);
    }
    else if (attribute == context::data_attr_kind::T)
    {
        if (!test_symbol_for_read(var_symbol, expr_subscript, vs->symbol_range, eval_ctx.diags))
            return std::string("U");

        context::SET_t value =
            eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, var_symbol, transform(expr_subscript)).access_c();

        if (value.access_c() != "U")
            return value;
        return evaluate_substituted(
            var_name, std::move(expr_subscript), vs->symbol_range, eval_ctx); // is type U, must substitute var sym
    }
    else
    {
        if (attribute == context::data_attr_kind::K
            && !test_symbol_for_read(var_symbol, expr_subscript, vs->symbol_range, eval_ctx.diags))
            return context::symbol_attributes::default_ca_value(attribute);
        return eval_ctx.hlasm_ctx.get_attribute_value_ca(attribute, var_symbol, transform(expr_subscript));
    }
}

context::SET_t ca_symbol_attribute::evaluate_substituted(context::id_index var_name,
    std::vector<context::A_t> expr_subscript,
    range var_range,
    const evaluation_context& eval_ctx) const
{
    context::SET_t substituted_name =
        get_var_sym_value(eval_ctx.hlasm_ctx, var_name, expr_subscript, var_range, eval_ctx.diags);

    if (substituted_name.type != context::SET_t_enum::C_TYPE)
    {
        if (attribute != context::data_attr_kind::O && attribute != context::data_attr_kind::T)
            eval_ctx.diags.add_diagnostic(diagnostic_op::error_E066(expr_range));
        return context::symbol_attributes::default_ca_value(attribute);
    }

    auto [valid, ord_name] =
        eval_ctx.hlasm_ctx.try_get_symbol_name(try_extract_leading_symbol(substituted_name.access_c()));

    if (!valid)
    {
        if (attribute != context::data_attr_kind::O && attribute != context::data_attr_kind::T)
            eval_ctx.diags.add_diagnostic(diagnostic_op::error_E065(expr_range));
        return context::symbol_attributes::default_ca_value(attribute);
    }
    else
        return evaluate_ordsym(ord_name, eval_ctx);
}

} // namespace hlasm_plugin::parser_library::expressions
