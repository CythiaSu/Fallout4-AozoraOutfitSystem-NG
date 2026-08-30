import fs from "node:fs";
import path from "node:path";

const packageRoot = path.resolve(process.argv[2]);
const profile = process.argv[3];
const view = path.join(packageRoot, "PrismaUI_F4", "views", "OutfitManager");
const source = path.join(view, "menu.html");
const output = path.join(view, `menu-qa-${profile}.html`);
const layout = fs.readFileSync(path.join(packageRoot, "F4SE", "Plugins", "OutfitManager", `ui-layout-${profile}.json`), "utf8");
const tuning = fs.readFileSync(path.join(packageRoot, "F4SE", "Plugins", "OutfitManager", "ui-tuning.json"), "utf8");
const injection = `<script>addEventListener("DOMContentLoaded",()=>{setOutfitManagerLayout(${layout});setOutfitManagerTuning(${tuning});setOutfitManagerState({slot:1,maxSlot:500,targetName:"Player",targetGender:1,targets:[{id:"player",name:"Player",sex:1,player:true}],slotSummaries:{},actualItems:[]});});</script>`;
fs.writeFileSync(output, fs.readFileSync(source, "utf8").replace("</body>", `${injection}</body>`), "utf8");
console.log(output);
