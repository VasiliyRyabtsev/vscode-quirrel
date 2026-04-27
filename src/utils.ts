import * as vs from 'vscode';
import * as fs from 'fs';
import * as path from 'path';

const LABEL_MAX_LEN = 50;
const MAX_FRAGMENTS = 3;

export function extractRequirePath(line: string, index: number):
    { begin: number, end: number; value: string } | null
{
  const pattern = /\brequire\s*\(\s*[$@]?\"([^\"]*)\"/g;
  let result: RegExpExecArray | null;
  while ((result = pattern.exec(line)) != null) {
    const value = result[1];
    // The capture ends right before the closing quote in result[0], so its
    // start is (match end) − 1 (closing quote) − value.length. Using
    // line.indexOf(value, result.index) would misfire when `value` happens
    // to also occur earlier in the matched prefix (e.g. require("require")).
    const begin = result.index + result[0].length - 1 - value.length;
    const end = begin + value.length;
    if (begin <= index && index <= end)
      return { begin, end, value };
  }
  return null;
}

export function extractImportPath(line: string, index: number):
    { begin: number, end: number; value: string } | null
{
  const pattern = /\b(?:import|from)\s+[$@]?\"([^\"]*)\"/g;
  let result: RegExpExecArray | null;
  while ((result = pattern.exec(line)) != null) {
    const value = result[1];
    const begin = result.index + result[0].length - 1 - value.length;
    const end = begin + value.length;
    if (begin <= index && index <= end)
      return { begin, end, value };
  }
  return null;
}

// Resolves a module-path string to one or more candidate files on disk.
//
// For mount-prefixed paths like "%ui/login/login_state.nut" (Dagor-style
// engine mounts the extension can't know about), the leading "%name/" is
// stripped and the workspace is searched for files whose path ends with the
// remaining suffix. Multiple matches may be returned — the caller surfaces
// them via VS Code's multi-result picker. Build dirs / vendor / etc. are
// excluded by the user's files.exclude / search.exclude settings.
//
// Otherwise, tries in order: the path itself if absolute; relative to the
// current document's directory; relative to each workspace folder root.
// Returns at most one match for the non-mount path.
export async function resolveModulePath(currentDocPath: string, requested: string): Promise<vs.Uri[]> {
  const mountMatch = requested.match(/^%[A-Za-z0-9_]+\/?(.*)$/);
  if (mountMatch) {
    const suffix = mountMatch[1];
    if (!suffix) return [];
    return await vs.workspace.findFiles(`**/${suffix}`, undefined, 32);
  }

  const normalized = path.normalize(requested);

  if (path.isAbsolute(normalized))
    return fs.existsSync(normalized) ? [vs.Uri.file(normalized)] : [];

  const relativeToDoc = path.join(path.dirname(currentDocPath), normalized);
  if (fs.existsSync(relativeToDoc))
    return [vs.Uri.file(relativeToDoc)];

  const folders = vs.workspace.workspaceFolders;
  if (folders) {
    for (const folder of folders) {
      const candidate = path.join(folder.uri.fsPath, normalized);
      if (fs.existsSync(candidate))
        return [vs.Uri.file(candidate)];
    }
  }

  return [];
}

export function extractKeyBefore(line: string, index: number):
    { begin: number, end: number; value: string; delimiter: string } | null
{
  // Negative lookahead on '=' avoids matching the first '=' of '==' (equality).
  const pattern = /(\w+)(\s*)=(?!=)/g;
  let result: RegExpExecArray | null;
  let found = null;
  while ((result = pattern.exec(line)) != null) {
    const value = result[1];
    const begin = line.indexOf(value, result.index);
    if (begin > index)
      break;

    const end = begin + value.length;
    const delimiter = result[2];
    found = { begin, end, value, delimiter };
  }
  return found;
}

export function normalizeBackslashes(backslashed: string): string {
  return backslashed ? backslashed.replace(/\\/g, '/') : backslashed;
}

export function compareFileName(part: string, fileName: string): number {
  if (fileName.indexOf(part) >= 0)
    return 1;

  part = part.toLowerCase();
  fileName = fileName.toLowerCase();
  if (fileName.indexOf(part) >= 0)
    return 2;

  let fragments = 1;
  let count = 0;
  let lastFound = -1;
  for (let index = 0; index < part.length; ++index) {
    let found = fileName.indexOf(part.charAt(index), lastFound + 1);
    if (found < 0)
      break;

    if (found > lastFound + 1)
      ++fragments;
    lastFound = found;
    ++count;
  }
  return count < part.length || MAX_FRAGMENTS < fragments ? 0 : fragments + 2;
}

export function truncatePath(path: string): string {
  const length = path.length;
  if (length <= LABEL_MAX_LEN)
    return path;

  // Prefer breaking at the last '/' so the filename stays intact, but fall back
  // to plain tail truncation if the filename alone wouldn't fit within the budget.
  let tailStart = path.lastIndexOf('/');
  if (tailStart < 0 || length - tailStart >= LABEL_MAX_LEN)
    tailStart = length - (LABEL_MAX_LEN - 1);

  const headLen = LABEL_MAX_LEN - 1 - (length - tailStart);
  return path.slice(0, Math.max(0, headLen)) + '…' + path.slice(tailStart);
}

export const dbgOutputChannel = vs.window.createOutputChannel("Quirrel");
