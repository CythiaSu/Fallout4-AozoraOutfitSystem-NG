# Aozora Outfit Management System NG 1.1.2

## 中文

### 更新内容

1. 优化 Steam Deck 手柄体验：
   - 套装命名页面打开后自动聚焦输入框。
   - 套装管理页面使用十字键或左摇杆左右翻页。
   - 不再使用 LT/RT 重复翻页。
   - 快捷换装窗口支持 Tab 和 Esc 退出。
2. 添加稳定的队友换装方法：
   - 服装管理器为队友生成完整服装实例，并加入队友背包。
   - 保留 OMOD、材质、颜色和其他改造实例数据。
   - 不调用队友自动装备、强制装备或装备评级逻辑。
   - 不提前卸下队友当前的默认服装。
   - 默认显示强提示，说明需要通过原版交易界面手动装备。

### 给队友换装的详细方法

1. 确保目标已经成为你的队友。
2. 打开青空服装管理系统，选择目标队友和保存的套装。
3. 执行换装后，系统会显示“服装已放入目标的背包”提示。
4. 关闭提示，与队友打开 Fallout 4 原版交易界面。
5. 在队友背包中找到刚刚生成的服装，手动选择并装备。
6. 装备完成后，可以正常保存游戏、读档和离开当前区域。

这个方案故意不拦截 NPC 成为队友瞬间的原版换装动作。因此，NPC 在成为队友之前更换的衣服，仍可能按照原版行为恢复；成为队友以后，再通过本系统加入并手动装备的服装会保持在队友身上。

### MCM

MCM「提示信息」中新增“队友换装强提示”。

- 默认开启，确保第一次使用时玩家能看到正确操作方法。
- 关闭后不再显示确认式强提示，但换装和背包写入逻辑不变。

普通 NPC 和玩家仍使用原来的自动装备流程。

## English

### Changes

1. Improved Steam Deck gamepad experience:
   - The rename dialog opens with the text input focused.
   - Outfit pages can be changed with D-pad left/right or the left stick.
   - LT/RT no longer duplicate outfit-page paging.
   - The Quick Outfit Switcher can be closed with both Tab and Esc.
2. Added a stable companion outfit workflow:
   - The manager creates a complete outfit instance and adds it to the companion's inventory.
   - OMODs, material swaps, color data, and other modification-instance data are preserved.
   - Companion auto-equip, force-equip, and equipment-rating logic are not called.
   - The companion's current vanilla outfit is not removed in advance.
   - A confirmation prompt explains that the item must be equipped through the vanilla trade menu.

### Detailed companion outfit method

1. Make sure the target has already become your companion.
2. Open Aozora Outfit Management System and select the companion and a saved outfit.
3. Apply the outfit. The system will report that the outfit was added to the target's inventory.
4. Close the prompt and open the vanilla Fallout 4 trade menu with the companion.
5. Find the generated outfit in the companion's inventory and equip it manually.
6. After equipping it, the game can be saved, loaded, or left and re-entered normally.

This workflow intentionally does not intercept the vanilla outfit reset that may happen at the exact moment an NPC becomes a companion. An outfit changed before recruitment may therefore still follow vanilla behavior; an outfit added and manually equipped after recruitment through this system is the outfit that is preserved.

### MCM

The MCM Information page adds Companion Outfit Prompt.

- Enabled by default so the correct manual-equipping method is visible the first time.
- It can be disabled later; disabling the prompt does not change outfit generation or inventory transfer.

Players and ordinary NPCs keep their existing automatic-equip behavior.
