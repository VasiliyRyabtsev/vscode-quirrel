import * as vs from 'vscode';

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
