// Renders the sport icons (app/icons/src/*.svg) to the PNGs the app loads
// (app/icons/*.png): white on transparent, 60 px, twice the size a tile
// draws them so they stay sharp. The app tints them at draw time.
//
//   node tools/build-icons.mjs
//
// Needs Playwright's Chromium. Run it after changing an icon and commit the
// PNGs; the TV build only copies them.
import { readFileSync, readdirSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const SIZE = 60;
const root = join(dirname(fileURLToPath(import.meta.url)), '..');
const srcDir = join(root, 'app/icons/src');
const outDir = join(root, 'app/icons');

const browser = await chromium.launch();
const page = await browser.newPage();
const names = readdirSync(srcDir).filter((f) => f.endsWith('.svg')).sort();
for (const file of names) {
  const svg = readFileSync(join(srcDir, file), 'utf8');
  const png = await page.evaluate(async ({ svg, size }) => {
    const img = new Image(size, size);
    img.src = 'data:image/svg+xml;base64,' + btoa(svg);
    await img.decode();
    const canvas = document.createElement('canvas');
    canvas.width = canvas.height = size;
    canvas.getContext('2d').drawImage(img, 0, 0, size, size);
    return canvas.toDataURL('image/png').split(',')[1];
  }, { svg, size: SIZE });
  writeFileSync(join(outDir, file.replace(/\.svg$/, '.png')), Buffer.from(png, 'base64'));
}
await browser.close();
console.log(`${names.length} icons -> app/icons`);
