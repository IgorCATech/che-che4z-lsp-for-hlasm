# Copyright (c) 2021 Broadcom.
# The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
#
# This program and the accompanying materials are made
# available under the terms of the Eclipse Public License 2.0
# which is available at https://www.eclipse.org/legal/epl-2.0/
#
# SPDX-License-Identifier: EPL-2.0
#
# Contributors:
#   Broadcom, Inc. - initial API and implementation

target_sources(parser_library PRIVATE
	aread_time.cpp
	aread_time.h
	analyzer.cpp
	analyzer.h
	analyzing_context.h
	compiler_options.cpp
	compiler_options.h
	diagnosable.h
	diagnosable_ctx.cpp
	diagnosable_ctx.h
	diagnosable_impl.h
	diagnostic.cpp
	diagnostic.h
	diagnostic_consumer.h
	diagnostic_adder.h
	ebcdic_encoding.cpp
	ebcdic_encoding.h
	error_messages.h
	instruction_set_version.h
	lib_config.cpp
	location.h
	preprocessor_options.h
	protocol.cpp
	tagged_index.h
	virtual_file_monitor.h
	workspace_manager.cpp
	workspace_manager_impl.h
)

add_subdirectory(checking)
add_subdirectory(config)
add_subdirectory(context)
add_subdirectory(debugging)
add_subdirectory(expressions)
add_subdirectory(lexing)
add_subdirectory(lsp)
add_subdirectory(parsing)
add_subdirectory(processing)
add_subdirectory(semantics)
add_subdirectory(workspaces)
