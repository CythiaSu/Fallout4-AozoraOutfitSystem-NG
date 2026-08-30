import { createRequire } from "node:module";
import fs from "node:fs";
import http from "node:http";
import path from "node:path";

const require = createRequire(import.meta.url);
const { chromium } = require("C:/Users/Sylva/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright");

const packageRoot = process.argv[2];
const outputRoot = process.argv[3];
if (!packageRoot || !outputRoot) throw new Error("Usage: verify-layout.mjs <package> <output>");

fs.mkdirSync(outputRoot, { recursive: true });
const root = path.resolve(packageRoot);
const mime = { ".html": "text/html", ".png": "image/png", ".json": "application/json", ".txt": "text/plain" };
const server = http.createServer((req, res) => {
  const pathname = decodeURIComponent(new URL(req.url, "http://127.0.0.1").pathname);
  const relative = pathname === "/ui-layout-16x9.json" ? "F4SE/Plugins/OutfitManager/ui-layout-16x9.json"
    : pathname === "/ui-layout-16x10.json" ? "F4SE/Plugins/OutfitManager/ui-layout-16x10.json"
    : pathname === "/ui-tuning.json" ? "F4SE/Plugins/OutfitManager/ui-tuning.json"
    : pathname.replace(/^\//, "");
  const file = path.resolve(root, relative);
  if (!file.startsWith(root) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "Content-Type": mime[path.extname(file)] || "application/octet-stream" });
  fs.createReadStream(file).pipe(res);
});
await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
const port = server.address().port;
const url = `http://127.0.0.1:${port}/PrismaUI_F4/views/OutfitManager/menu.html`;
const viewports = [
  ["16x9-1920x1080", 1920, 1080],
  ["16x10-1920x1200", 1920, 1200],
  ["21x9-2560x1080", 2560, 1080],
];

const browser = await chromium.launch({ headless: true, executablePath: "C:/Program Files/Google/Chrome/Application/chrome.exe" });
const results = [];
for (const [name, width, height] of viewports) {
  const page = await browser.newPage({ viewport: { width, height } });
  const profile = width / height >= 1.55 && width / height <= 1.67 ? "16x10" : "16x9";
  await page.goto(`http://127.0.0.1:${port}/PrismaUI_F4/views/OutfitManager/menu-qa-${profile}.html`, { waitUntil: "domcontentloaded", timeout: 10000 });
  await page.waitForTimeout(800);
  const check = await page.evaluate(() => {
    const visible = [...document.querySelectorAll("body *")].filter(el => {
      const s = getComputedStyle(el);
      const r = el.getBoundingClientRect();
      return s.display !== "none" && s.visibility !== "hidden" && r.width > 0 && r.height > 0;
    });
    const clipped = visible.filter(el => {
      if (el.classList.contains("scroll")) return false;
      return el.scrollWidth > el.clientWidth + 2 || el.scrollHeight > el.clientHeight + 2;
    }).map(el => ({ tag: el.tagName, id: el.id, className: String(el.className || ""), text: String(el.textContent || "").trim().slice(0, 100), client: [el.clientWidth, el.clientHeight], scroll: [el.scrollWidth, el.scrollHeight] })).slice(0, 30);
    const app = document.querySelector(".app")?.getBoundingClientRect();
    return {
      profile: document.documentElement.dataset.layoutProfile,
      ready: document.documentElement.dataset.layoutReady,
      app: app ? { x: app.x, y: app.y, width: app.width, height: app.height } : null,
      clipped,
      mascotComplete: [...document.images].filter(img => img.offsetParent !== null).every(img => img.complete && img.naturalWidth > 0),
    };
  });
  await page.screenshot({ path: path.join(outputRoot, `${name}.png`), fullPage: true });
  results.push({ name, ...check });
  await page.close();
}
await browser.close();
await new Promise(resolve => server.close(resolve));
fs.writeFileSync(path.join(outputRoot, "layout-report.json"), JSON.stringify(results, null, 2));
console.log(JSON.stringify(results, null, 2));
