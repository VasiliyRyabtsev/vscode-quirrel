import * as vs from 'vscode';
import syntaxDiags from './syntaxDiags';
import { isParserInitialized, analyzeCode, AnalysisResult } from './quirrelParser';

const ERRORCODE_UNUSED = [213, 221, 228];

const versionControl: {[key: string]: number;} = {};

function applyDiagnostics(document: vs.TextDocument, result: AnalysisResult) {
  const messages = result.messages;

  const diagList: vs.Diagnostic[] = messages.map((msg) => {
    const line = Math.max(0, msg.line - 1);
    const col = Math.max(0, msg.col);
    const len = Math.max(1, msg.len);
    const range = new vs.Range(line, col, line, col + len);
    const severity = msg.isError
      ? vs.DiagnosticSeverity.Error
      : vs.DiagnosticSeverity.Warning;

    // Clean up message - remove redundant "ERROR: " prefix if present
    let message = msg.message;
    if (msg.isError && message.startsWith('ERROR: ')) {
      message = message.substring(7);
    }

    const diag = new vs.Diagnostic(range, message, severity);

    // Handle missing/invalid error codes (parse errors have intId=-1, textId="")
    const errorCode = msg.intId >= 0 ? msg.intId : undefined;
    const textId = msg.textId || (msg.isError ? 'syntax-error' : 'warning');
    diag.code = errorCode !== undefined ? `${errorCode}:${textId}` : textId;

    if (errorCode !== undefined && ERRORCODE_UNUSED.indexOf(errorCode) >= 0)
      diag.tags = [vs.DiagnosticTag.Unnecessary];
    return diag;
  });

  syntaxDiags.set(document.uri, diagList);
  return diagList.length;
}

export default function checkSyntaxOnSave(document: vs.TextDocument) {
  if (document.languageId !== 'quirrel')
    return;

  if (!isParserInitialized()) {
    return;
  }

  const srcPath = document.uri.fsPath;
  const version = document.version;
  if (versionControl[srcPath] === version)
    return;
  versionControl[srcPath] = version;

  const result = analyzeCode(document.getText());
  applyDiagnostics(document, result);
}

export function checkSyntaxCommand() {
  const editor = vs.window.activeTextEditor;
  if (!editor) {
    vs.window.showWarningMessage('No active editor');
    return;
  }

  const document = editor.document;
  if (document.languageId !== 'quirrel') {
    vs.window.showWarningMessage('Not a Quirrel file');
    return;
  }

  if (!isParserInitialized()) {
    vs.window.showErrorMessage('WASM parser not initialized');
    return;
  }

  const result = analyzeCode(document.getText());
  const count = applyDiagnostics(document, result);

  if (count === 0) {
    vs.window.showInformationMessage('No issues found');
  } else {
    vs.window.showInformationMessage(`Found ${count} issue(s)`);
  }
}
