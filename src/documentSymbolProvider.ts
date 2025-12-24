import * as vs from 'vscode';
import { parseAndExtractSymbols, isParserInitialized, QuirrelSymbol } from './quirrelParser';

const KIND_MAP: Record<string, vs.SymbolKind> = {
    'Function': vs.SymbolKind.Function,
    'Method': vs.SymbolKind.Method,
    'Constructor': vs.SymbolKind.Constructor,
    'Class': vs.SymbolKind.Class,
    'Enum': vs.SymbolKind.Enum,
    'EnumMember': vs.SymbolKind.EnumMember,
    'Variable': vs.SymbolKind.Variable,
    'Binding': vs.SymbolKind.Constant,  // let x = ... (immutable binding)
    'Constant': vs.SymbolKind.Constant, // const X = ... (compile-time constant)
    'Property': vs.SymbolKind.Property,
    'Field': vs.SymbolKind.Field,
    'Object': vs.SymbolKind.Object,
};

function toRange(sym: QuirrelSymbol): vs.Range {
    // Quirrel uses 1-based lines, VS Code uses 0-based
    return new vs.Range(
        Math.max(0, sym.range.startLine - 1),
        Math.max(0, sym.range.startCol),
        Math.max(0, sym.range.endLine - 1),
        Math.max(0, sym.range.endCol)
    );
}

function toDocumentSymbol(sym: QuirrelSymbol): vs.DocumentSymbol {
    const range = toRange(sym);
    const kind = KIND_MAP[sym.kind] ?? vs.SymbolKind.Null;

    const symbol = new vs.DocumentSymbol(
        sym.name,
        '',  // detail
        kind,
        range,
        range  // selectionRange same as range
    );

    if (sym.children && sym.children.length > 0) {
        symbol.children = sym.children.map(toDocumentSymbol);
    }

    return symbol;
}

export class QuirrelDocumentSymbolProvider implements vs.DocumentSymbolProvider {
    provideDocumentSymbols(
        document: vs.TextDocument,
        _token: vs.CancellationToken
    ): vs.ProviderResult<vs.DocumentSymbol[]> {
        if (!isParserInitialized()) {
            return [];
        }

        const result = parseAndExtractSymbols(document.getText());

        if (result.error) {
            // Don't log parse errors - they're expected for incomplete code
            return [];
        }

        return result.symbols.map(toDocumentSymbol);
    }
}
