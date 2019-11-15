#include "collector.h"
#include "shared/lexer.h"
#include "range_provider.h"

using namespace hlasm_plugin::parser_library;
using namespace hlasm_plugin::parser_library::semantics;


collector::collector()
	: lsp_symbols_extracted_(false), hl_symbols_extracted_(false) {}

const label_si& collector::current_label()
{
	return *lbl_;
}

const instruction_si& collector::current_instruction()
{
	return *instr_;
}

const operands_si& collector::current_operands()
{
	return *op_;
}

const remarks_si& collector::current_remarks()
{
	return *rem_;
}

void collector::set_label_field(range symbol_range)
{
	if (lbl_)
		throw std::runtime_error("field already assigned");
	lbl_.emplace(symbol_range);
}

void collector::set_label_field(std::string label, range symbol_range)
{
	if (lbl_)
		throw std::runtime_error("field already assigned");
	lbl_.emplace(symbol_range, std::move(label), label_si::mac_flag());
}

void collector::set_label_field(seq_sym sequence_symbol, range symbol_range)
{
	if (lbl_)
		throw std::runtime_error("field already assigned");
	lbl_.emplace(symbol_range, std::move(sequence_symbol));
}

void collector::set_label_field(context::id_index label, antlr4::ParserRuleContext * parser_ctx, range symbol_range)
{
	if (lbl_)
		throw std::runtime_error("field already assigned");
	//recognise, whether label consists only of ORDSYMBOL token
	if (!parser_ctx || (parser_ctx->getStart() == parser_ctx->getStop() && parser_ctx->getStart()->getType() == lexer::Tokens::ORDSYMBOL))
	{
		lbl_.emplace(symbol_range ,*label );
		add_lsp_symbol({ *label, symbol_range, symbol_type::ord });
	}
	//recognise, whether label consists of DOT ORDSYMBOL tokens, so it is sequence symbol
	else if (parser_ctx->children.size() == 2 && parser_ctx->getStart()->getType() == lexer::Tokens::DOT && parser_ctx->getStop()->getType() == lexer::Tokens::ORDSYMBOL)
	{
		seq_sym ss = { label, symbol_range };
		lbl_.emplace(symbol_range,ss );
	}
	//otherwise it is macro label parameter
	else
	{
		lbl_.emplace(symbol_range, *label, label_si::mac_flag());
		add_lsp_symbol({ *label, symbol_range, symbol_type::ord });
	}
}

void collector::set_label_field(concat_chain label, range symbol_range)
{
	if (lbl_)
		throw std::runtime_error("field already assigned");
	concatenation_point::clear_concat_chain(label);
	if (label.size() == 1 && label[0]->type == concat_type::VAR) //label is variable symbol
	{
		auto vs = std::unique_ptr<var_sym>(label[0]->access_var());
		label[0].release();
		lbl_.emplace(symbol_range, std::move(vs));
	}
	else //label is concatenation
	{
		lbl_.emplace(symbol_range, std::move(label));
	}
}

void collector::set_instruction_field(range symbol_range)
{
	if (instr_)
		throw std::runtime_error("field already assigned");
	instr_.emplace(symbol_range);
}

void collector::set_instruction_field(context::id_index instr, range symbol_range)
{
	if (instr_)
		throw std::runtime_error("field already assigned");
	instr_.emplace(symbol_range, instr);
}

void collector::set_instruction_field(concat_chain instr, range symbol_range)
{
	if (instr_)
		throw std::runtime_error("field already assigned");
	instr_.emplace(symbol_range, std::move(instr));
}

void collector::set_operand_remark_field(range symbol_range)
{
	if (op_ || rem_ ||def_)
		throw std::runtime_error("field already assigned");
	op_.emplace(symbol_range,operand_list());
	rem_.emplace(symbol_range, std::vector<range>());
	def_.emplace("", symbol_range);
}

void collector::set_operand_remark_field(std::string deferred, std::vector<range> remarks, range symbol_range)
{
	if (op_ || rem_ || def_)
		throw std::runtime_error("field already assigned");
	def_.emplace(std::move(deferred), symbol_range);
	rem_.emplace(symbol_range, std::move(remarks));
}

void collector::set_operand_remark_field(std::vector<operand_ptr> operands, std::vector<range> remarks, range symbol_range)
{
	if (op_ || rem_ || def_)
		throw std::runtime_error("field already assigned");
	op_.emplace(symbol_range, std::move(operands));
	rem_.emplace(symbol_range,std::move(remarks));
}

void collector::add_lsp_symbol(lsp_symbol symbol)
{
	lsp_symbols_.push_back(std::move(symbol));
}

void collector::add_hl_symbol(token_info symbol)
{
		hl_symbols_.push_back(std::move(symbol));
}

void collector::clear_hl_lsp_symbols()
{
	lsp_symbols_.clear();
	hl_symbols_.clear();
}

void collector::add_operands_hl_symbols()
{
	if (!op_)
		return;
	for (auto&& operand : current_operands().value)
	{
		if (operand)
			add_hl_symbol(token_info(operand->operand_range, hl_scopes::operand));
	}
}

void collector::add_remarks_hl_symbols()
{
	if (!rem_)
		return;
	for (auto remark : current_remarks().value)
	{
		add_hl_symbol(token_info(remark, hl_scopes::remark));
	}
}

void collector::append_reparsed_symbols(collector&& c)
{
	for (auto& sym : c.hl_symbols_)
		hl_symbols_.push_back(std::move(sym));
	
	size_t i;
	for (i = 0; i < lsp_symbols_.size() && lsp_symbols_[i].type != symbol_type::instruction; ++i);

	if (i != lsp_symbols_.size())
		while(lsp_symbols_.size() != i + 1)
			lsp_symbols_.pop_back();

	for (auto& s : c.lsp_symbols_)
		lsp_symbols_.push_back(std::move(s));
	
}

const instruction_si& collector::peek_instruction()
{
	return *instr_;
}

std::variant<statement_si, statement_si_deferred> collector::extract_statement(bool deferred_hint, range default_range)
{
	if (!lbl_)
		lbl_.emplace(default_range);
	if (!instr_)
		instr_.emplace(default_range);
	if(!rem_)
		rem_.emplace(default_range, remark_list{});

	assert(!deferred_hint || !(op_ && !op_->value.empty()));

	if (deferred_hint)
	{
		if (!def_)
			def_.emplace("", instr_->field_range);
		return statement_si_deferred(
			range_provider::union_range(lbl_->field_range, def_->second),
			std::move(*lbl_), std::move(*instr_), std::move(def_.value().first), def_.value().second);
	}
	else
	{
		if (!op_)
			op_.emplace(instr_->field_range, operand_list{});

		//foreach operand substitute null with empty
		for (size_t i = 0; i < op_->value.size(); i++)
		{
			if (!op_->value[i])
				op_->value[i] = std::make_unique<empty_operand>(instr_->field_range);
		}

		range r = range_provider::union_range(lbl_->field_range, op_->field_range);
		return statement_si(r, std::move(*lbl_), std::move(*instr_), std::move(*op_), std::move(*rem_));
	}
}

std::vector<lsp_symbol> collector::extract_lsp_symbols()
{
	if (lsp_symbols_extracted_)
		throw std::runtime_error("data already extracted");

	lsp_symbols_extracted_ = true;
	return std::move(lsp_symbols_);
}

std::vector<token_info> collector::extract_hl_symbols()
{
	if (hl_symbols_extracted_)
		throw std::runtime_error("data already extracted");

	hl_symbols_extracted_ = true;
	return std::move(hl_symbols_);
}

void collector::prepare_for_next_statement()
{
	lbl_.reset();
	instr_.reset();
	op_.reset();
	rem_.reset();
	def_.reset();
	
	lsp_symbols_.clear();
	hl_symbols_.clear();
	lsp_symbols_extracted_ = false;
	hl_symbols_extracted_ = false;
}
