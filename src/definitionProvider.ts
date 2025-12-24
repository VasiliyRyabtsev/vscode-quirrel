import * as vs from 'vscode';
import { findDeclarationAt, isParserInitialized, DeclarationLocation } from './quirrelParser';

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
    provideDefinition(
        document: vs.TextDocument,
        position: vs.Position,
        _token: vs.CancellationToken
    ): vs.ProviderResult<vs.Definition> {
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

        const range = toRange(result.location);
        return new vs.Location(document.uri, range);
    }
}
