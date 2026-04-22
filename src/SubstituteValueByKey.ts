import * as vs from 'vscode';
import { promises as fs } from 'fs';
import { extractKeyBefore } from './utils';
import { mapKeyValue, KeyDescriptor } from './mapKeyValue';

export class SubstituteValueByKey implements vs.CompletionItemProvider {

  provideCompletionItems(
    document: vs.TextDocument, position: vs.Position,
    token: vs.CancellationToken, context: vs.CompletionContext
  ) {
    const line = document.lineAt(position).text;
    const key = extractKeyBefore(line, position.character);
    if (key == null)
      return undefined;

    let values = mapKeyValue[key.value];
    let alias;
    while ((alias = (values as KeyDescriptor).alias) != undefined)
      values = mapKeyValue[alias];
    if (values == null)
      return undefined;

    // Extend the replace range back over any identifier prefix the user has
    // already typed after '=', so selecting a value replaces that prefix
    // instead of appending after it.
    let wordStart = position.character;
    while (wordStart > 0 && /\w/.test(line.charAt(wordStart - 1)))
      wordStart--;

    // Mirror the key-to-'=' spacing on the value side, but only when '=' is
    // directly adjacent to the insertion point — otherwise whitespace/prefix
    // already sits between '=' and the value and a prepended delimiter would
    // duplicate it.
    const equalsEnd = key.end + key.delimiter.length + 1;
    const insertPrefix = equalsEnd === wordStart ? key.delimiter : "";

    const range = new vs.Range(position.line, wordStart, position.line, position.character);
    const valuesList: string[] = typeof values === "string" ? [values] : (values as string[]);
    const result: vs.CompletionItem[] = valuesList.map((value) => {
      const item =  new vs.CompletionItem(value);
      item.kind = vs.CompletionItemKind.Value;
      item.insertText = `${insertPrefix}${value}`;
      item.range = range;
      return item;
    });
    return result;
  }

}