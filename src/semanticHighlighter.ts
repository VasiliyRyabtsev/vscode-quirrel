import * as vs from 'vscode';
import { extractSemanticTokens, isParserInitialized } from './quirrelParser';
import { dbgOutputChannel } from './utils';

// Token type indices (must match C++ enum order)
const TT_VARIABLE = 0;
const TT_PARAMETER = 1;
const TT_FUNCTION = 2;
// const TT_CLASS = 3;
const TT_ENUM = 4;
// const TT_ENUM_MEMBER = 5;
// const TT_PROPERTY = 6;
const TT_IMPORT = 7;

// We colorize local identifiers and imports
// Classes, enums, properties keep their TextMate colors

// Default number of distinct colors in the auto-generated palette
const DEFAULT_PALETTE_SIZE = 12;

// Generate a color palette based on theme type
function generatePalette(isDark: boolean, size: number, saturation: number): string[] {
    const colors: string[] = [];
    // Use HSL with varying hue, fixed saturation and lightness appropriate for theme
    const lightness = isDark ? 70 : 35;

    for (let i = 0; i < size; i++) {
        // Spread hues evenly across the color wheel, starting from different points
        // to avoid similar colors being adjacent
        const hue = (i * 360 / size + 15) % 360;
        colors.push(`hsl(${hue}, ${saturation}%, ${lightness}%)`);
    }
    return colors;
}

// Hash a string to get a consistent index
function hashString(str: string): number {
    let hash = 0;
    for (let i = 0; i < str.length; i++) {
        const char = str.charCodeAt(i);
        hash = ((hash << 5) - hash) + char;
        hash = hash & hash; // Convert to 32bit integer
    }
    return Math.abs(hash);
}

interface ParameterStyle {
    bold: boolean;
    italic: boolean;
    underline: boolean;
}

export class QuirrelSemanticHighlighter implements vs.Disposable {
    private _decorationTypes: Map<number, vs.TextEditorDecorationType> = new Map();
    private _disposables: vs.Disposable[] = [];
    private _debounceTimer: NodeJS.Timeout | undefined;
    private _currentPalette: string[] = [];
    private _isDarkTheme: boolean = true;
    private _enabled: boolean = true;
    private _parameterStyle: ParameterStyle = { bold: true, italic: false, underline: false };

    constructor() {
        this._loadSettings();
        this._updatePalette();

        // Listen for document changes
        this._disposables.push(
            vs.workspace.onDidChangeTextDocument(e => {
                if (this._enabled && e.document.languageId === 'quirrel' && e.contentChanges.length > 0) {
                    this._scheduleUpdate();
                }
            })
        );

        // Listen for active editor changes
        this._disposables.push(
            vs.window.onDidChangeActiveTextEditor(editor => {
                if (this._enabled && editor && editor.document.languageId === 'quirrel') {
                    this._updateDecorations(editor);
                }
            })
        );

        // Listen for theme changes - regenerate palette
        this._disposables.push(
            vs.window.onDidChangeActiveColorTheme(() => {
                this._updatePalette();
                this._recreateDecorationTypes();
                if (this._enabled) {
                    const editor = vs.window.activeTextEditor;
                    if (editor && editor.document.languageId === 'quirrel') {
                        this._updateDecorations(editor);
                    }
                }
            })
        );

        // Listen for configuration changes
        this._disposables.push(
            vs.workspace.onDidChangeConfiguration(e => {
                if (e.affectsConfiguration('quirrel.semanticHighlighting')) {
                    const wasEnabled = this._enabled;
                    this._loadSettings();
                    this._updatePalette();
                    this._recreateDecorationTypes();

                    const editor = vs.window.activeTextEditor;
                    if (editor && editor.document.languageId === 'quirrel') {
                        if (this._enabled) {
                            this._updateDecorations(editor);
                        } else if (wasEnabled) {
                            // Was enabled, now disabled - clear decorations
                            this._clearDecorations(editor);
                        }
                    }
                }
            })
        );
    }

    private _loadSettings() {
        const config = vs.workspace.getConfiguration('quirrel.semanticHighlighting');
        this._enabled = config.get<boolean>('enabled', true);
        this._parameterStyle = {
            bold: config.get<boolean>('parameterStyle.bold', true),
            italic: config.get<boolean>('parameterStyle.italic', false),
            underline: config.get<boolean>('parameterStyle.underline', false),
        };
    }

    private _updatePalette() {
        const config = vs.workspace.getConfiguration('quirrel.semanticHighlighting');
        const customColors = config.get<string[] | null>('colors', null);

        if (customColors && customColors.length > 0) {
            // Use custom palette
            this._currentPalette = customColors;
        } else {
            // Auto-generate based on theme
            const themeKind = vs.window.activeColorTheme.kind;
            this._isDarkTheme = (themeKind === vs.ColorThemeKind.Dark || themeKind === vs.ColorThemeKind.HighContrast);
            const saturation = config.get<number>('saturation', 50);
            this._currentPalette = generatePalette(this._isDarkTheme, DEFAULT_PALETTE_SIZE, saturation);
        }
    }

    private _recreateDecorationTypes() {
        // Dispose old decoration types
        for (const dt of this._decorationTypes.values()) {
            dt.dispose();
        }
        this._decorationTypes.clear();

        if (this._parameterStyleDecorationType) {
            this._parameterStyleDecorationType.dispose();
            this._parameterStyleDecorationType = null;
        }
    }

    private _getDecorationTypeForColor(colorIndex: number): vs.TextEditorDecorationType {
        const paletteSize = this._currentPalette.length;
        const actualIndex = colorIndex % paletteSize;

        if (!this._decorationTypes.has(actualIndex)) {
            const color = this._currentPalette[actualIndex];
            const decorationType = vs.window.createTextEditorDecorationType({
                color: color
            });
            this._decorationTypes.set(actualIndex, decorationType);
        }
        return this._decorationTypes.get(actualIndex)!;
    }

    private _parameterStyleDecorationType: vs.TextEditorDecorationType | null = null;

    private _getParameterStyleDecorationType(): vs.TextEditorDecorationType | null {
        const { bold, italic, underline } = this._parameterStyle;

        // No styling if all options are disabled
        if (!bold && !italic && !underline) {
            return null;
        }

        if (!this._parameterStyleDecorationType) {
            const options: vs.DecorationRenderOptions = {};

            if (bold) {
                options.fontWeight = 'bold';
            }
            if (italic) {
                options.fontStyle = 'italic';
            }
            if (underline) {
                options.textDecoration = 'underline';
            }

            this._parameterStyleDecorationType = vs.window.createTextEditorDecorationType(options);
        }
        return this._parameterStyleDecorationType;
    }

    dispose() {
        if (this._debounceTimer) {
            clearTimeout(this._debounceTimer);
        }
        this._disposables.forEach(d => d.dispose());
        this._recreateDecorationTypes();
    }

    // Called when WASM parser becomes ready
    refresh() {
        if (!this._enabled) return;
        const editor = vs.window.activeTextEditor;
        if (editor && editor.document.languageId === 'quirrel') {
            this._updateDecorations(editor);
        }
    }

    private _scheduleUpdate() {
        if (this._debounceTimer) {
            clearTimeout(this._debounceTimer);
        }
        this._debounceTimer = setTimeout(() => {
            if (!this._enabled) return;
            const editor = vs.window.activeTextEditor;
            if (editor && editor.document.languageId === 'quirrel') {
                this._updateDecorations(editor);
            }
        }, 100);
    }

    private _clearDecorations(editor: vs.TextEditor) {
        for (const dt of this._decorationTypes.values()) {
            editor.setDecorations(dt, []);
        }
        if (this._parameterStyleDecorationType) {
            editor.setDecorations(this._parameterStyleDecorationType, []);
        }
    }

    private _updateDecorations(editor: vs.TextEditor) {
        if (!this._enabled) {
            this._clearDecorations(editor);
            return;
        }

        if (!isParserInitialized()) {
            dbgOutputChannel.appendLine('Semantic highlighter: parser not initialized');
            return;
        }

        const result = extractSemanticTokens(editor.document.getText());
        //dbgOutputChannel.appendLine(`Semantic highlighter: got ${result.tokens?.length || 0} tokens`);

        // Group ranges by identifier name -> color index
        // We need to get the actual identifier text from the document
        const paletteSize = this._currentPalette.length;
        const rangesByColorIndex: Map<number, vs.Range[]> = new Map();
        const parameterRanges: vs.Range[] = [];

        if (result.tokens) {
            for (const token of result.tokens) {
                // Colorize variables/constants, parameters, local functions, enums, and imports
                if (token.type !== TT_VARIABLE &&
                    token.type !== TT_PARAMETER &&
                    token.type !== TT_FUNCTION &&
                    token.type !== TT_IMPORT &&
                    token.type !== TT_ENUM) {
                    continue;
                }

                // Convert 1-based line to 0-based
                const line = Math.max(0, token.line - 1);
                const range = new vs.Range(line, token.col, line, token.col + token.length);

                // Get the identifier text to compute its color
                const identifierText = editor.document.getText(range);
                const colorIndex = hashString(identifierText) % paletteSize;

                // All tokens get color
                if (!rangesByColorIndex.has(colorIndex)) {
                    rangesByColorIndex.set(colorIndex, []);
                }
                rangesByColorIndex.get(colorIndex)!.push(range);

                // Parameters additionally get font style
                if (token.type === TT_PARAMETER) {
                    parameterRanges.push(range);
                }
            }
        }

        // Clear all existing decorations first
        this._clearDecorations(editor);

        // Apply color decorations to all tokens
        for (const [colorIndex, ranges] of rangesByColorIndex) {
            const decorationType = this._getDecorationTypeForColor(colorIndex);
            editor.setDecorations(decorationType, ranges);
        }

        // Apply font style decoration to parameters (layered on top of color)
        const paramStyleDecor = this._getParameterStyleDecorationType();
        if (paramStyleDecor && parameterRanges.length > 0) {
            editor.setDecorations(paramStyleDecor, parameterRanges);
        }
    }
}
