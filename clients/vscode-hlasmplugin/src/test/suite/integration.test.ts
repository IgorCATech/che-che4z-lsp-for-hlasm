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

import * as assert from 'assert';
import * as vscode from 'vscode';
import * as path from 'path';
import * as helper from './testHelper';

suite('Integration Test Suite', () => {
    const workspace_file = 'open';
    let editor: vscode.TextEditor;

    suiteSetup(async function () {
        this.timeout(30000);

        await helper.showDocument(workspace_file);
        editor = helper.get_editor(workspace_file);
    });

    // open 'open' file, should be recognized as hlasm
    test('HLASM file open test', async () => {
        // setting a language takes a while but shouldn't take longer than a second
        await helper.sleep(1000);
        assert.ok(editor.document.languageId === 'hlasm');
    }).timeout(10000).slow(4000);

    // change 'open' file to create diagnostic
    test('Diagnostic test', async () => {
        // register callback to check for the correctness of the diagnostic
        const diagnostic_event = new Promise<[vscode.Uri, vscode.Diagnostic[]][]>((resolve, reject) => {
            const listener = vscode.languages.onDidChangeDiagnostics((_) => {
                listener.dispose();
                resolve(vscode.languages.getDiagnostics());
            });
        });
        // remove second parameter from LR instruction
        await editor.edit(edit => {
            edit.delete(new vscode.Range(new vscode.Position(2, 6), new vscode.Position(2, 7)));
        });

        const allDiags = await diagnostic_event;
        const openDiags = allDiags.find(pair => pair[0].path.endsWith("open"))[1]

        assert.ok(openDiags.length == 1 && openDiags[0].code == 'M003', 'Wrong diagnostic');
    }).timeout(10000).slow(1000);

    // test completion for instructions
    test('Completion Instructions test', async () => {
        await helper.insertString(editor, new vscode.Position(7, 1), 'L');

        await vscode.commands.executeCommand('editor.action.triggerSuggest');
        await helper.sleep(1000);

        await vscode.commands.executeCommand('acceptSelectedSuggestion');
        await helper.sleep(1000);

        const text = editor.document.getText();
        const acceptedLine = text.split('\n')[7];

        assert.ok(acceptedLine.includes('L   R,D12U(X,B)'), 'Wrong suggestion result' + acceptedLine);
    }).timeout(10000).slow(4000);

    // test completion for variable symbols
    test('Completion Variable symbol test', async () => {
        await helper.insertString(editor, new vscode.Position(8, 0), '&');

        await vscode.commands.executeCommand('editor.action.triggerSuggest');
        await helper.sleep(1000);

        await vscode.commands.executeCommand('acceptSelectedSuggestion')
        await helper.sleep(1000);

        const text = editor.document.getText();
        const acceptedLine = text.split('\n')[8];

        assert.ok(acceptedLine.includes('&VAR'), 'Wrong suggestion result' + acceptedLine);
    }).timeout(10000).slow(4000);

    // go to definition for ordinary symbol
    test('Definition Ordinary symbol test', async () => {
        const result: vscode.Location[] = await vscode.commands.executeCommand('vscode.executeDefinitionProvider', editor.document.uri, new vscode.Position(1, 7));

        assert.ok(result.length == 1
            && result[0].uri.fsPath == editor.document.fileName
            && result[0].range.start.line == 9
            && result[0].range.start.character == 0, 'Wrong ordinary symbol definition location');
    }).timeout(10000).slow(1000);

    // hover for variable symbol
    test('Hover Variable symbol test', async () => {
        const result: vscode.Hover[] = await vscode.commands.executeCommand('vscode.executeHoverProvider', editor.document.uri, new vscode.Position(6, 8));

        assert.ok(result.length == 1
            && result[0].contents.length == 1
            && (result[0].contents[0] as vscode.MarkdownString).value == 'SETA variable', 'Wrong variable symbol hover contents');
    }).timeout(10000).slow(1000);

    // go to definition for macros
    test('Definition Macro test', async () => {
        const result: vscode.Location[] = await vscode.commands.executeCommand('vscode.executeDefinitionProvider', editor.document.uri, new vscode.Position(6, 2));
        assert.ok(result.length == 1
            && result[0].uri.fsPath == path.join(helper.getWorkspacePath(), 'libs', 'mac.asm')
            && result[0].range.start.line == 1
            && result[0].range.start.character == 4, 'Wrong macro definition location');
    }).timeout(10000).slow(1000);

    // debug open code test
    test('Debug test', async () => {
        const session_started_event = new Promise<vscode.DebugSession>((resolve) => {
            // when the debug session starts
            const disposable = vscode.debug.onDidStartDebugSession((session) => {
                disposable.dispose();
                resolve(session);
            });
        });
        // start debugging
        if (!await vscode.debug.startDebugging(vscode.workspace.workspaceFolders[0], 'Macro tracer: current program'))
            throw new Error("Failed to start a debugging session");

        const session = await session_started_event;

        // wait a second to let the debug session complete
        await helper.sleep(1000);
        // step over once
        await vscode.commands.executeCommand('workbench.action.debug.stepOver');
        // wait 1 more second to let step over take place
        await helper.sleep(1000);
        // then check for VAR2 variable
        const scopesResult = await session.customRequest('scopes', { frameId: 0 });

        const scopes = scopesResult.body ? scopesResult.body.scopes : scopesResult.scopes;

        const reference = scopes.find((scope: { name: string }) => scope.name == 'Locals').variablesReference;
        const variablesResult = await session.customRequest('variables', { variablesReference: reference });

        await vscode.commands.executeCommand('workbench.action.debug.stop');

        const variables = variablesResult.body ? variablesResult.body.variables : variablesResult.variables;

        assert.ok(variables.length == 1 && variables[0].value == 'SOMETHING' && variables[0].name == '&VAR2', 'Wrong debug variable &VAR2');
    }).timeout(10000).slow(4000);

    // verify that library patterns are working
    test('Test library patterns', async () => {
        const files = await vscode.workspace.findFiles('pattern_test/test_pattern.hlasm');

        assert.ok(files && files[0]);
        const file = files[0];

        // open the file
        const document = await vscode.workspace.openTextDocument(file);

        await vscode.window.showTextDocument(document);

        await helper.sleep(2000);

        const allDiags = vscode.languages.getDiagnostics();
        const patternDiags = allDiags.find(pair => pair[0].path.endsWith("test_pattern.hlasm"))

        if (patternDiags)
            assert.ok(patternDiags[1].length == 0, 'Library patterns are not working');
    }).timeout(10000).slow(2500);
});
