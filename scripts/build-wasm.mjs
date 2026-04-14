import { mkdirSync } from 'fs';
import { execSync } from 'child_process';

const buildDir = 'wasm/build';
mkdirSync(buildDir, { recursive: true });

const isWin = process.platform === 'win32';
const generator = isWin ? 'NMake Makefiles' : 'Unix Makefiles';
const make = isWin ? 'nmake' : 'make';

execSync(`emcmake cmake -G "${generator}" ..`, { cwd: buildDir, stdio: 'inherit' });
execSync(`emmake ${make}`, { cwd: buildDir, stdio: 'inherit' });
