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

#ifndef HLASMPARSER_PARSERLIBRARY_ANALYZING_CONTEXT_H
#define HLASMPARSER_PARSERLIBRARY_ANALYZING_CONTEXT_H

#include "context/hlasm_context.h"
#include "lsp/lsp_context.h"

namespace hlasm_plugin::parser_library {


struct analyzing_context
{
    std::shared_ptr<context::hlasm_context> hlasm_ctx;
    std::shared_ptr<lsp::lsp_context> lsp_ctx;
};

} // namespace hlasm_plugin::parser_library
#endif
