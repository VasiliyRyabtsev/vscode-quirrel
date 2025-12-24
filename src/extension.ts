import * as vs from 'vscode';

import { SubstitutePathByFile } from './SubstitutePathByFile';
import { SubstituteValueByKey } from './SubstituteValueByKey';
import openFileByPath from './openFileByPath';
import runDocumentCode from './runDocumentCode';
import checkSyntaxOnSave, { checkSyntaxCommand } from './checkSyntaxOnSave';
import clearDiagsOnClose from './clearDiagsOnClose';
import { initParser } from './quirrelParser';
import { QuirrelDocumentSymbolProvider } from './documentSymbolProvider';
import { QuirrelDefinitionProvider } from './definitionProvider';
import { dbgOutputChannel } from './utils';

const DOCUMENT: vs.DocumentSelector = { language: 'quirrel', scheme: 'file' };

export async function activate(context: vs.ExtensionContext) {
  // Initialize WASM parser (non-blocking, symbols work after load)
  initParser(context.extensionPath).catch(err => {
    dbgOutputChannel.appendLine(`WASM parser initialization failed: ${err}`);
    dbgOutputChannel.appendLine('Document symbols will not be available.');
  });

  // Register document symbol provider
  const symbolProvider: vs.Disposable = vs.languages.registerDocumentSymbolProvider(
    DOCUMENT, new QuirrelDocumentSymbolProvider());
  context.subscriptions.push(symbolProvider);

  // Register definition provider (Go To Declaration)
  const definitionProvider: vs.Disposable = vs.languages.registerDefinitionProvider(
    DOCUMENT, new QuirrelDefinitionProvider());
  context.subscriptions.push(definitionProvider);
  const completePath: vs.Disposable = vs.languages.registerCompletionItemProvider(
    DOCUMENT, new SubstitutePathByFile(), '"', '/', '\\');
  context.subscriptions.push(completePath);

  const completeValue: vs.Disposable = vs.languages.registerCompletionItemProvider(
    DOCUMENT, new SubstituteValueByKey(), '=');
  context.subscriptions.push(completeValue);

  const commandOpenByPath: vs.Disposable = vs.commands.registerCommand(
    'quirrel.editor.action.openModule', openFileByPath);
  context.subscriptions.push(commandOpenByPath);

  const commandRunDocumentCode: vs.Disposable = vs.commands.registerCommand(
    'quirrel.editor.action.runCode', runDocumentCode);
  context.subscriptions.push(commandRunDocumentCode);

  const commandCheckSyntax: vs.Disposable = vs.commands.registerCommand(
    'quirrel.editor.action.checkSyntax', checkSyntaxCommand);
  context.subscriptions.push(commandCheckSyntax);

  vs.workspace.onDidSaveTextDocument(checkSyntaxOnSave);
  vs.workspace.onDidCloseTextDocument(clearDiagsOnClose);
}

export function deactivate() {
  return undefined;
}