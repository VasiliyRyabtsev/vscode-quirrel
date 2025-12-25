import * as path from 'path';

export interface SymbolRange {
    startLine: number;
    startCol: number;
    endLine: number;
    endCol: number;
}

export interface QuirrelSymbol {
    name: string;
    kind: string;
    range: SymbolRange;
    children?: QuirrelSymbol[];
}

export interface ParseResult {
    error: string | null;
    symbols: QuirrelSymbol[];
}

export interface DiagnosticItem {
    line: number;
    col: number;
    len: number;
    file: string;
    intId: number;
    textId: string;
    message: string;
    isError: boolean;
}

export interface AnalysisResult {
    messages: DiagnosticItem[];
}

export interface DeclarationLocation {
    line: number;    // 1-based
    col: number;     // 0-based
    endLine: number; // 1-based
    endCol: number;  // 0-based
    kind: string;
}

export interface FindDeclarationResult {
    found: boolean;
    location?: DeclarationLocation;
}

export interface SemanticToken {
    line: number;      // 1-based
    col: number;       // 0-based
    length: number;
    type: number;      // Token type index
    modifiers: number; // Modifier bitmask
}

export interface SemanticTokensResult {
    tokens: SemanticToken[];
}

interface QuirrelWasmModule {
    parseAndExtractSymbols(source: string): string;
    analyzeCode(source: string): string;
    findDeclarationAt(source: string, line: number, col: number): string;
    extractSemanticTokens(source: string): string;
}

let wasmModule: QuirrelWasmModule | null = null;
let initPromise: Promise<void> | null = null;

export async function initParser(extensionPath: string): Promise<void> {
    if (wasmModule) {
        return;
    }

    if (initPromise) {
        return initPromise;
    }

    initPromise = (async () => {
        const wasmJsPath = path.join(extensionPath, 'out', 'quirrel-vscode.js');

        try {
            // Dynamic import for ES module
            const moduleFactory = await import(wasmJsPath);
            wasmModule = await moduleFactory.default();
        } catch (e) {
            // Fallback: try require for CommonJS compatibility
            try {
                const moduleFactory = require(wasmJsPath);
                wasmModule = await moduleFactory();
            } catch (e2) {
                throw new Error(`Failed to load WASM module: ${e}`);
            }
        }
    })();

    return initPromise;
}

export function isParserInitialized(): boolean {
    return wasmModule !== null;
}

export function parseAndExtractSymbols(source: string): ParseResult {
    if (!wasmModule) {
        return {
            error: 'Parser not initialized. Call initParser() first.',
            symbols: []
        };
    }

    try {
        const jsonResult = wasmModule.parseAndExtractSymbols(source);
        return JSON.parse(jsonResult) as ParseResult;
    } catch (e) {
        return {
            error: `Parse error: ${e}`,
            symbols: []
        };
    }
}

export function analyzeCode(source: string): AnalysisResult {
    if (!wasmModule) {
        return { messages: [] };
    }

    try {
        const jsonResult = wasmModule.analyzeCode(source);
        return JSON.parse(jsonResult) as AnalysisResult;
    } catch (e) {
        return { messages: [] };
    }
}

export function findDeclarationAt(source: string, line: number, col: number): FindDeclarationResult {
    if (!wasmModule) {
        return { found: false };
    }

    try {
        const jsonResult = wasmModule.findDeclarationAt(source, line, col);
        return JSON.parse(jsonResult) as FindDeclarationResult;
    } catch (e) {
        return { found: false };
    }
}

export function extractSemanticTokens(source: string): SemanticTokensResult {
    if (!wasmModule) {
        return { tokens: [] };
    }

    try {
        const jsonResult = wasmModule.extractSemanticTokens(source);
        return JSON.parse(jsonResult) as SemanticTokensResult;
    } catch (e) {
        return { tokens: [] };
    }
}
