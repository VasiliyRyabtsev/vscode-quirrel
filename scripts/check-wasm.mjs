// Checks that the WASM build output is not stale relative to native sources.
// Usage:
//   node scripts/check-wasm.mjs          # Check freshness (used in vscode:prepublish)
//   node scripts/check-wasm.mjs --save   # Save build marker (run after WASM build)

import { statSync, readdirSync, readFileSync, writeFileSync, existsSync } from 'fs';
import { join, resolve } from 'path';
import { execSync } from 'child_process';

const ROOT = resolve(import.meta.dirname, '..');
const WASM_OUTPUT = join(ROOT, 'out', 'quirrel-vscode.wasm');
const WASM_DIR = join(ROOT, 'wasm');
const MARKER_FILE = join(ROOT, 'out', '.wasm-build-marker');
const SOURCE_EXTENSIONS = ['.cpp', '.h', '.txt'];

function getSubmoduleCommit() {
  try {
    return execSync('git rev-parse HEAD:quirrel', { cwd: ROOT, encoding: 'utf8' }).trim();
  } catch {
    return null;
  }
}

// --save mode: record current submodule commit after a successful WASM build
if (process.argv.includes('--save')) {
  const commit = getSubmoduleCommit();
  if (commit) {
    writeFileSync(MARKER_FILE, commit + '\n');
    console.log(`WASM build marker saved (quirrel @ ${commit.slice(0, 8)})`);
  }
  process.exit(0);
}

// Check mode: verify WASM output is fresh
function fail(reason) {
  console.error('');
  console.error('  WASM build is stale!');
  console.error(`  ${reason}`);
  console.error('');
  console.error('  Run:  npm run compile:wasm');
  console.error('');
  process.exit(1);
}

// 1. WASM output must exist
if (!existsSync(WASM_OUTPUT)) {
  fail('out/quirrel-vscode.wasm not found.');
}

// 2. No source file in wasm/ should be newer than the output
const outputMtime = statSync(WASM_OUTPUT).mtimeMs;

for (const file of readdirSync(WASM_DIR)) {
  if (SOURCE_EXTENSIONS.some(ext => file.endsWith(ext))) {
    const filePath = join(WASM_DIR, file);
    const mtime = statSync(filePath).mtimeMs;
    if (mtime > outputMtime) {
      fail(`wasm/${file} is newer than the WASM output.`);
    }
  }
}

// 3. Quirrel submodule commit should match the one from the last build
const currentCommit = getSubmoduleCommit();
if (currentCommit) {
  if (existsSync(MARKER_FILE)) {
    const savedCommit = readFileSync(MARKER_FILE, 'utf8').trim();
    if (currentCommit !== savedCommit) {
      fail(`Quirrel submodule changed since last WASM build (${savedCommit.slice(0, 8)} \u2192 ${currentCommit.slice(0, 8)}).`);
    }
  } else {
    // No marker yet — source files are fresh, so record the current state.
    // This handles manual WASM builds that bypass npm run compile:wasm.
    writeFileSync(MARKER_FILE, currentCommit + '\n');
    console.log(`WASM build marker auto-saved (quirrel @ ${currentCommit.slice(0, 8)})`);
  }
}

console.log('WASM build check: OK');
