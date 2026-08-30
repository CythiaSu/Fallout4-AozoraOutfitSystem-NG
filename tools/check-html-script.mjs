import fs from "node:fs";
import vm from "node:vm";

const html = fs.readFileSync(process.argv[2], "utf8");
const scripts = [...html.matchAll(/<script(?:\s[^>]*)?>([\s\S]*?)<\/script>/gi)].map(match => match[1]).filter(Boolean);
for (let index = 0; index < scripts.length; index++) new vm.Script(scripts[index], { filename: `${process.argv[2]}#script-${index + 1}` });
console.log(`Validated ${scripts.length} inline scripts`);
