// Usage: node test-grammar.mjs [snippet]
// If no snippet provided, runs built-in test cases

import { readFileSync } from 'fs';
import { resolve, dirname } from 'path';
import { fileURLToPath } from 'url';
import oniguruma from 'vscode-oniguruma';
const { createOnigScanner, createOnigString, loadWASM } = oniguruma;
import vsctm from 'vscode-textmate';
const { Registry, INITIAL } = vsctm;

const __dirname = dirname(fileURLToPath(import.meta.url));

const wasmBin = readFileSync(resolve(__dirname, 'node_modules/vscode-oniguruma/release/onig.wasm'));
await loadWASM(wasmBin.buffer);

const registry = new Registry({
  onigLib: Promise.resolve({ createOnigScanner, createOnigString }),
  loadGrammar: async (scopeName) => {
    if (scopeName === 'source.quirrel') {
      const grammarPath = resolve(__dirname, 'syntaxes/quirrel.tmLanguage.json');
      const content = readFileSync(grammarPath, 'utf-8');
      return JSON.parse(content);
    }
    return null;
  }
});

const grammar = await registry.loadGrammar('source.quirrel');

function tokenizeLine(line, ruleStack) {
  return grammar.tokenizeLine(line, ruleStack);
}

function showTokens(code) {
  const lines = code.split('\n');
  let ruleStack = INITIAL;
  for (const line of lines) {
    const result = tokenizeLine(line, ruleStack);
    console.log(`\n>>> ${line}`);
    for (const token of result.tokens) {
      const text = line.substring(token.startIndex, token.endIndex);
      const scopes = token.scopes.filter(s => s !== 'source.quirrel').join(' ');
      console.log(`  ${String(token.startIndex).padStart(3)}-${String(token.endIndex).padStart(3)}  ${JSON.stringify(text).padEnd(16)} ${scopes}`);
    }
    ruleStack = result.ruleStack;
  }
}

// Test snippets
const snippetArg = process.argv[2];
if (snippetArg) {
  showTokens(snippetArg);
} else {
  const tests = [
    '// --- Function params (normal) ---',
    'function foo(x, y) {}',
    '',
    '// --- Destructuring in function params ---',
    'function des1({x}) { return x + 1 }',
    'function des2([x]) { return x + 1 }',
    'function des5(x, {y = null}) { return x + (y ?? 2) }',
    'function des6(x, {y = null, z: int|float = 1000}) { return x + (y ?? 2) + z }',
    'function des7([x: function = @() @() @() null, t, r: string = "abc555", g = "abc666"], {y = null, z: int|float = 1000,}, ...) {}',
    '',
    '// --- Destructuring in let/local ---',
    'let {x, y} = foo()',
    'let [a, b] = arr',
    'local {abc} = cn3',
    'local [d1, d2] = e3',
    'local { y: int = 609, x: string|int|null } = t2',
  ];

  for (const t of tests) {
    if (t.startsWith('//') || t === '') {
      console.log(t);
    } else {
      showTokens(t);
      console.log();
    }
  }
}
