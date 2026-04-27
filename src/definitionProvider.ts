import * as vs from 'vscode';
import { findDeclarationAt, isParserInitialized, DeclarationLocation } from './quirrelParser';
import { extractRequirePath, extractImportPath, resolveModulePath } from './utils';

function toRange(loc: DeclarationLocation): vs.Range {
    // Quirrel uses 1-based lines, VS Code uses 0-based
    return new vs.Range(
        Math.max(0, loc.line - 1),
        Math.max(0, loc.col),
        Math.max(0, loc.endLine - 1),
        Math.max(0, loc.endCol)
    );
}

export class QuirrelDefinitionProvider implements vs.DefinitionProvider {
    async provideDefinition(
        document: vs.TextDocument,
        position: vs.Position,
        _token: vs.CancellationToken
    ): Promise<vs.LocationLink[] | null> {
        // Module-path navigation: if the cursor is inside a string literal of
        // require("…"), import "…", or from "…" import …, jump to the file(s).
        const line = document.lineAt(position.line).text;
        const modulePath = extractRequirePath(line, position.character)
                        ?? extractImportPath(line, position.character);
        if (modulePath) {
            const uris = await resolveModulePath(document.fileName, modulePath.value);
            if (uris.length === 0)
                return null;
            const originSelectionRange = new vs.Range(
                position.line, modulePath.begin,
                position.line, modulePath.end
            );
            const targetRange = new vs.Range(0, 0, 0, 0);
            return uris.map(uri => ({
                originSelectionRange,
                targetUri: uri,
                targetRange,
            }));
        }

        if (!isParserInitialized()) {
            return null;
        }

        // Convert VS Code 0-based line to Quirrel 1-based
        const quirrelLine = position.line + 1;
        const quirrelCol = position.character;

        const result = findDeclarationAt(document.getText(), quirrelLine, quirrelCol);

        if (!result.found || !result.location) {
            return null;
        }

        const targetRange = toRange(result.location);
        // originSelectionRange omitted — VS Code falls back to the word at the cursor.
        return [{
            targetUri: document.uri,
            targetRange,
        }];
    }
}
