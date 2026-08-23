ScriptName OutfitManager Extends Quest

Bool Property bForceCompile = True Auto

Actor Function _GetTargetActor() Global
    Actor akActionTarget = OMNative.GetMenuActionTarget()
    If akActionTarget != None
        Return akActionTarget
    EndIf

    Actor akMenuTarget = OMNative.GetMenuTarget()
    If akMenuTarget != None
        Return akMenuTarget
    EndIf

    ObjectReference akRef = GardenOfEden3.GetCameraTargetReference()
    Actor akGoeTarget = akRef as Actor
    If akGoeTarget != None && !akGoeTarget.IsDead()
        Return akGoeTarget
    EndIf

    Actor akTarget = OMNative.GetCameraTarget()
    If akTarget == None || akTarget.IsDead()
        Return Game.GetPlayer()
    EndIf
    Return akTarget
EndFunction

Actor Function _ResolveMenuActionTarget(Actor akForcedTarget = None) Global
    ObjectReference akTargetRef = OMNative.GetMenuActionTargetRef()
    Actor akTarget = akTargetRef as Actor
    If akTarget != None
        Return akTarget
    EndIf

    akTarget = OMNative.GetMenuActionTarget()
    If akTarget != None
        Return akTarget
    EndIf

    If akForcedTarget != None
        Return akForcedTarget
    EndIf

    Return _GetTargetActor()
EndFunction

String Function _GetTargetName(Actor akTarget) Global
    If akTarget == Game.GetPlayer()
        Return "玩家"
    EndIf

    String sName = OMNative.GetActorName(akTarget)
    If sName == ""
        Return "未知NPC"
    EndIf
    Return sName
EndFunction

Int Function _GetSex(Actor akTarget) Global
    If akTarget == None
        Return -1
    EndIf

    ActorBase akBase = akTarget.GetLeveledActorBase()
    If akBase == None
        Return -1
    EndIf

    Return akBase.GetSex()
EndFunction

String Function _SexText(Int aiSex) Global
    If aiSex == 0
        Return "男性"
    ElseIf aiSex == 1
        Return "女性"
    EndIf
    Return "未知"
EndFunction

Int Function _MaxOutfitSlots() Global
    Return 500
EndFunction

Float Function _RandomCooldownSeconds() Global
    Return 6.0
EndFunction

Float Function _RandomPrepareSeconds() Global
    Return 1.0
EndFunction

Float Function _RestoreCooldownSeconds() Global
    Return 4.5
EndFunction

Float Function _RestorePrepareSeconds() Global
    Return 0.05
EndFunction

Float Function _MenuOpenCooldownSeconds() Global
    Return 1.0
EndFunction

Float Function _MenuActionCooldownSeconds() Global
    Return 0.6
EndFunction

Float Function _MenuPollSeconds() Global
    Return 0.08
EndFunction

Bool Function _OutfitInfoEnabled() Global
    Return OMNative.AreOutfitNotificationsEnabled()
EndFunction

Function _NotifyOutfitInfo(String asText) Global
    If _OutfitInfoEnabled()
        Debug.Notification(asText)
    EndIf
EndFunction

Function _LogTarget(String asLabel, Actor akTarget) Global
    If akTarget == None
        OMNative.LogText(asLabel + " target=None")
        Return
    EndIf
    OMNative.LogText(asLabel + " target=" + OMNative.GetActorName(akTarget) + " isPlayer=" + (akTarget == Game.GetPlayer()))
EndFunction

Bool Function _IsPowerArmorBlocked(Actor akTarget) Global
    If akTarget != None && akTarget.IsInPowerArmor()
        Debug.Notification("服装管理器不能在动力甲状态下保存或换装。请先离开动力甲。")
        Return True
    EndIf
    Return False
EndFunction

String Function _SlotPreviewText(Int aiSlot) Global
    String sStatus = "空"
    String sDetails = ""

    If OMNative.HasSlotData(aiSlot)
        sStatus = "已保存"
        sDetails = "\n性别：" + _SexText(OMNative.GetSlotGender(aiSlot)) + "\n套装：" + OMNative.GetSavedOutfitSummary(aiSlot)
    EndIf

    Return "当前套装槽位：" + aiSlot + " (" + sStatus + ")" + sDetails
EndFunction

Bool Function _ContainsForm(Form[] akForms, Int aiCount, Form akItem) Global
    Int i = 0
    While i < aiCount
        If akForms[i] == akItem
            Return True
        EndIf
        i += 1
    EndWhile
    Return False
EndFunction

Form Function _FormFromID(Int aiFormID) Global
    If aiFormID == 0
        Return None
    EndIf
    Return Game.GetForm(aiFormID)
EndFunction

Form Function _GetSavedOutfitItem(Int aiSlot, Int aiIndex) Global
    Return _FormFromID(OMNative.GetSavedOutfitItemID(aiSlot, aiIndex))
EndFunction

Form Function _GetManagedItem(Actor akTarget, Int aiIndex) Global
    Return _FormFromID(OMNative.GetManagedItemID(akTarget, aiIndex))
EndFunction

Form Function _GetDefaultOutfitItem(Actor akTarget, Int aiIndex) Global
    Return _FormFromID(OMNative.GetDefaultOutfitItemID(akTarget, aiIndex))
EndFunction

Int Function _UnequipCurrentOutfit(Actor akTarget) Global
    If akTarget == None
        Return 0
    EndIf

    Form[] unequippedItems = new Form[44]
    Int iUnequippedCount = 0
    Int iSlot = 0

    While iSlot < 44
        Actor:WornItem kWorn = akTarget.GetWornItem(iSlot)
        If kWorn.item != None && OMNative.IsOutfitItem(kWorn.item) && !_ContainsForm(unequippedItems, iUnequippedCount, kWorn.item)
            akTarget.UnequipItem(kWorn.item, False, True)
            unequippedItems[iUnequippedCount] = kWorn.item
            iUnequippedCount += 1
            Utility.Wait(0.02)
        EndIf
        iSlot += 1
    EndWhile

    Utility.Wait(0.1)
    Return iUnequippedCount
EndFunction

Int Function _UnequipOutfitItemsExcept(Actor akTarget, Form[] akKeepItems, Int aiKeepCount) Global
    If akTarget == None
        Return 0
    EndIf

    Form[] unequippedItems = new Form[44]
    Int iUnequippedCount = 0
    Int iSlot = 0

    While iSlot < 44
        Actor:WornItem kWorn = akTarget.GetWornItem(iSlot)
        If kWorn.item != None && OMNative.IsOutfitItem(kWorn.item) && !_ContainsForm(akKeepItems, aiKeepCount, kWorn.item) && !_ContainsForm(unequippedItems, iUnequippedCount, kWorn.item)
            akTarget.UnequipItem(kWorn.item, False, True)
            unequippedItems[iUnequippedCount] = kWorn.item
            iUnequippedCount += 1
            Utility.Wait(0.02)
        EndIf
        iSlot += 1
    EndWhile

    Return iUnequippedCount
EndFunction

Int Function _RemoveManagedItemsExcept(Actor akTarget, Form[] akKeepItems, Int aiKeepCount) Global
    If akTarget == None
        Return 0
    EndIf

    Int iRemoved = 0
    Int i = 0
    Int iCount = OMNative.GetManagedItemCount(akTarget)
    While i < iCount
        Form kItem = _GetManagedItem(akTarget, i)
        If kItem != None && OMNative.IsOutfitItem(kItem) && !_ContainsForm(akKeepItems, aiKeepCount, kItem) && akTarget.GetItemCount(kItem) > 0
            akTarget.UnequipItem(kItem, False, True)
            akTarget.RemoveItem(kItem, 1, True, None)
            iRemoved += 1
            Utility.Wait(0.03)
        EndIf
        i += 1
    EndWhile

    Return iRemoved
EndFunction

Bool Function _IsWearingForm(Actor akTarget, Form akItem) Global
    If akTarget == None || akItem == None
        Return False
    EndIf

    Int iSlot = 0
    While iSlot < 44
        Actor:WornItem kWorn = akTarget.GetWornItem(iSlot)
        If kWorn.item == akItem
            Return True
        EndIf
        iSlot += 1
    EndWhile

    Return False
EndFunction

Bool Function _IsWearingSavedOutfit(Int aiSlot, Int aiCount, Actor akTarget) Global
    If akTarget == None || aiCount <= 0
        Return False
    EndIf

    Int i = 0
    Int iValid = 0
    While i < aiCount
        Form kItem = _GetSavedOutfitItem(aiSlot, i)
        If kItem != None && OMNative.IsOutfitItem(kItem)
            iValid += 1
            If !_IsWearingForm(akTarget, kItem)
                Return False
            EndIf
        EndIf
        i += 1
    EndWhile

    Return iValid > 0
EndFunction

Function _FinishEquipUpdate(Actor akTarget) Global
    If akTarget == None
        Return
    EndIf

    If akTarget != Game.GetPlayer()
        akTarget.QueueUpdate(True, 12)
    EndIf
EndFunction

Int Function _EquipSavedOutfit(Int aiSlot, Int aiCount, Actor akTarget) Global
    If akTarget == None || aiCount <= 0
        Return 0
    EndIf

    Form[] keepItems = new Form[44]
    Int iKeepCount = 0
    Int iEquipped = 0
    Int i = 0
    If akTarget != Game.GetPlayer()
        OMNative.EnsureDefaultOutfitRecorded(akTarget)
    EndIf

    While i < aiCount
        Form kKeepItem = _GetSavedOutfitItem(aiSlot, i)
        If kKeepItem != None && !_ContainsForm(keepItems, iKeepCount, kKeepItem)
            keepItems[iKeepCount] = kKeepItem
            iKeepCount += 1
        EndIf
        i += 1
    EndWhile

    i = 0
    While i < aiCount
        Form kItem = _GetSavedOutfitItem(aiSlot, i)
        If kItem != None
            If akTarget.GetItemCount(kItem) <= 0
                akTarget.AddItem(kItem, 1, True)
                OMNative.RecordManagedItem(akTarget, kItem)
            EndIf
            akTarget.EquipItem(kItem, False, False)
            Utility.Wait(0.05)
            If _IsWearingForm(akTarget, kItem)
                iEquipped += 1
            EndIf
        EndIf
        i += 1
    EndWhile

    _UnequipOutfitItemsExcept(akTarget, keepItems, iKeepCount)
    _RemoveManagedItemsExcept(akTarget, keepItems, iKeepCount)
    _FinishEquipUpdate(akTarget)
    Return iEquipped
EndFunction

String Function _EquipSavedOutfitClean(Int aiSlot, Int aiCount, Actor akTarget) Global
    _LogTarget("EquipClean start slot=" + aiSlot + " count=" + aiCount, akTarget)
    If akTarget == None || aiCount <= 0
        Return "已装备：0 / " + aiCount
    EndIf

    Form[] keepItems = new Form[44]
    Int iKeepCount = 0
    Int iEquipped = 0
    Int i = 0
    If akTarget != Game.GetPlayer()
        OMNative.EnsureDefaultOutfitRecorded(akTarget)
    EndIf

    While i < aiCount
        Form kKeepItem = _GetSavedOutfitItem(aiSlot, i)
        If kKeepItem != None && !_ContainsForm(keepItems, iKeepCount, kKeepItem)
            keepItems[iKeepCount] = kKeepItem
            iKeepCount += 1
        EndIf
        i += 1
    EndWhile

    i = 0
    While i < aiCount
        Form kItem = _GetSavedOutfitItem(aiSlot, i)
        If kItem != None
            If akTarget.GetItemCount(kItem) <= 0
                akTarget.AddItem(kItem, 1, True)
                OMNative.RecordManagedItem(akTarget, kItem)
            EndIf

            akTarget.EquipItem(kItem, False, False)
            Utility.Wait(0.08)

            Bool bWearing = _IsWearingForm(akTarget, kItem)
            Bool bPlayerWearing = _IsWearingForm(Game.GetPlayer(), kItem)
            OMNative.LogText("EquipClean item=" + kItem + " targetWearing=" + bWearing + " playerWearing=" + bPlayerWearing)
            If bWearing
                iEquipped += 1
            EndIf
        EndIf

        i += 1
    EndWhile

    _UnequipOutfitItemsExcept(akTarget, keepItems, iKeepCount)
    _RemoveManagedItemsExcept(akTarget, keepItems, iKeepCount)
    _FinishEquipUpdate(akTarget)
    Return "已装备：" + iEquipped + " / " + aiCount
EndFunction

Function _ResetNpcOutfitFromMenu(Actor akForcedTarget = None) Global
    Actor akTarget = _ResolveMenuActionTarget(akForcedTarget)
    _LogTarget("ResetNpc resolved", akTarget)
    If akTarget == None || akTarget == Game.GetPlayer()
        Debug.Notification("重置 NPC 服装只对有效 NPC 目标生效。")
        Return
    EndIf

    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("操作冷却中。")
        Return
    EndIf

    Int iDefaultCount = OMNative.GetDefaultOutfitCount(akTarget)
    If iDefaultCount <= 0
        Debug.Notification("没有记录到该 NPC 的默认服装。")
        Return
    EndIf

    Form[] keepItems = new Form[44]
    Int iKeepCount = 0
    Int i = 0
    While i < iDefaultCount
        Form kItem = _GetDefaultOutfitItem(akTarget, i)
        If kItem != None && !_ContainsForm(keepItems, iKeepCount, kItem)
            keepItems[iKeepCount] = kItem
            iKeepCount += 1
            If akTarget.GetItemCount(kItem) <= 0
                akTarget.AddItem(kItem, 1, True)
            EndIf
            akTarget.EquipItem(kItem, False, False)
            Utility.Wait(0.08)
        EndIf
        i += 1
    EndWhile

    _UnequipOutfitItemsExcept(akTarget, keepItems, iKeepCount)
    _RemoveManagedItemsExcept(akTarget, keepItems, iKeepCount)
    OMNative.ClearManagedItems(akTarget)
    _FinishEquipUpdate(akTarget)
    _NotifyOutfitInfo("已还原 NPC 默认服装。")
EndFunction

Function _ClearSlotFromMenu(Int aiSlot) Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("操作冷却中。")
        Return
    EndIf

    Int iSlot = aiSlot
    If iSlot < 1 || iSlot > _MaxOutfitSlots()
        iSlot = OMNative.GetCurrentSlot()
    EndIf

    If OMNative.ClearSlotData(iSlot)
        _NotifyOutfitInfo("已清空槽位 " + iSlot + "。")
    Else
        Debug.Notification("清空槽位失败。")
    EndIf
EndFunction

Function _SetSlotFromMenu(Int aiSlot) Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), 0.2)
        Debug.Notification("操作冷却中。")
        Return
    EndIf

    Int iSlot = aiSlot
    If iSlot < 1
        iSlot = _MaxOutfitSlots()
    ElseIf iSlot > _MaxOutfitSlots()
        iSlot = 1
    EndIf

    OMNative.SetCurrentSlot(iSlot)
EndFunction

Function _SaveSelectedMenuTarget(Int aiSlot) Global
    String sTargetName = OMNative.GetMenuActionTargetName()
    If OMNative.IsMenuActionTargetPowerArmorBlocked()
        Debug.MessageBox("保存失败。\n目标正在使用动力装甲。\n\n请先离开动力装甲，再保存常规套装。")
        Return
    EndIf
    Int iResult = OMNative.SaveMenuActionTargetOutfit(aiSlot)
    If iResult > 0
        Debug.MessageBox("套装已保存。\n目标：" + sTargetName + "\n槽位：" + aiSlot + "\n物品数：" + iResult + "\n\n套装：" + OMNative.GetSavedOutfitSummary(aiSlot))
    Else
        Debug.MessageBox("保存失败。\n目标：" + sTargetName + "\n槽位：" + aiSlot + "\n未检测到有效服装。")
    EndIf
EndFunction

Function _LoadSelectedMenuTarget(Int aiSlot) Global
    If OMNative.IsEquipBusy()
        Debug.Notification("正在更换套装。")
        Return
    EndIf
    If OMNative.GetEquipCooldownRemaining(Utility.GetCurrentRealTime()) > 0.0
        Debug.Notification("还原功能冷却中。")
        Return
    EndIf
    If !OMNative.HasSlotData(aiSlot)
        Debug.Notification("还原失败。\n槽位 " + aiSlot + " 为空。")
        Return
    EndIf
    If OMNative.IsMenuActionTargetPowerArmorBlocked()
        Return
    EndIf
    Int iResult = OMNative.LoadMenuActionTargetOutfit(aiSlot)
    If iResult > 0 && OMNative.IsMenuActionTargetWearingOutfit(aiSlot)
        Debug.Notification("已穿戴此套装。")
        Return
    EndIf
    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("还原功能冷却中。")
        Return
    EndIf
    _NotifyOutfitInfo("正在还原套装...")
    Utility.Wait(_RestorePrepareSeconds())
    iResult = OMNative.EquipMenuActionTargetOutfit(aiSlot)
    If iResult == -5 || iResult == -8
        OMNative.FinishEquipAction()
        Return
    ElseIf iResult == -6
        OMNative.FinishEquipAction()
        Debug.Notification("战斗中不能换装")
        Return
    ElseIf iResult == -7
        OMNative.FinishEquipAction()
        Debug.Notification("目标正由任务或特殊状态控制，暂时不能换装")
        Return
    EndIf
    If iResult > 0
        OMNative.SetLastEquippedSlot(aiSlot)
        OMNative.FinishEquipAction()
        _NotifyOutfitInfo("套装已还原。\n目标：" + OMNative.GetMenuActionTargetName() + "\n槽位：" + aiSlot + "\n已穿戴：" + iResult + "\n\n套装：" + OMNative.GetSavedOutfitSummary(aiSlot))
    Else
        OMNative.FinishEquipAction()
        Debug.Notification("还原失败。\n槽位：" + aiSlot)
    EndIf
EndFunction

Function _RandomSelectedMenuTarget() Global
    If OMNative.IsEquipBusy() || OMNative.IsRandomBusy()
        Debug.Notification("正在随机还原套装。")
        Return
    EndIf
    If OMNative.GetRandomCooldownRemaining(Utility.GetCurrentRealTime()) > 0.0
        Debug.Notification("随机功能冷却中。")
        Return
    EndIf
    If OMNative.IsMenuActionTargetPowerArmorBlocked()
        Return
    EndIf
    Int iTargetSex = OMNative.GetMenuActionTargetSex()
    Int iChosenSlot = OMNative.ChooseRandomSlot(iTargetSex, 0, OMNative.GetLastRandomSlot())
    If iChosenSlot == 0
        Debug.Notification("随机还原失败。\n没有符合目标性别的套装：" + OMNative.GetMenuActionTargetName())
        Return
    EndIf
    If !OMNative.BeginRandom(Utility.GetCurrentRealTime(), _RandomCooldownSeconds())
        Debug.Notification("随机功能冷却中。")
        Return
    EndIf
    _NotifyOutfitInfo("正在选择套装...")
    Utility.Wait(_RandomPrepareSeconds())
    Int iResult = OMNative.EquipMenuActionTargetOutfit(iChosenSlot)
    If iResult == -5 || iResult == -8
        OMNative.FinishRandom(0)
        Return
    ElseIf iResult == -6
        OMNative.FinishRandom(0)
        Debug.Notification("战斗中不能换装")
        Return
    ElseIf iResult == -7
        OMNative.FinishRandom(0)
        Debug.Notification("目标正由任务或特殊状态控制，暂时不能换装")
        Return
    EndIf
    If iResult > 0
        OMNative.SetLastEquippedSlot(iChosenSlot)
        OMNative.FinishRandom(iChosenSlot)
        _NotifyOutfitInfo("随机套装已还原。\n目标：" + OMNative.GetMenuActionTargetName() + "\n槽位：" + iChosenSlot + "\n已穿戴：" + iResult + "\n\n套装：" + OMNative.GetSavedOutfitSummary(iChosenSlot))
    Else
        OMNative.FinishRandom(0)
        Debug.Notification("随机还原失败。\n槽位：" + iChosenSlot)
    EndIf
EndFunction

Function _ResetSelectedMenuTarget() Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("操作冷却中。")
        Return
    EndIf
    Int iResult = OMNative.ResetMenuActionTargetOutfit()
    If iResult > 0
        _NotifyOutfitInfo("目标服装已重置。")
    Else
        Debug.Notification("没有记录该目标的默认服装或已管理物品。")
    EndIf
EndFunction

Function _HandleMenuAction(Int aiAction, Int aiSlot) Global
    If aiAction == 1
        If OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
            OMNative.SetCurrentSlot(aiSlot)
            _SaveSelectedMenuTarget(aiSlot)
            OMNative.CloseMenu()
        Else
            Debug.Notification("操作冷却中。")
        EndIf
    ElseIf aiAction == 2
        OMNative.SetCurrentSlot(aiSlot)
        OMNative.CloseMenu()
        _LoadSelectedMenuTarget(aiSlot)
    ElseIf aiAction == 3
        OMNative.CloseMenu()
        _RandomSelectedMenuTarget()
    ElseIf aiAction == 4
        OMNative.CloseMenu()
        _ClearSlotFromMenu(aiSlot)
    ElseIf aiAction == 5
        OMNative.CloseMenu()
        _ResetSelectedMenuTarget()
    ElseIf aiAction == 6
        _SetSlotFromMenu(OMNative.GetCurrentSlot() - 1)
    ElseIf aiAction == 7
        _SetSlotFromMenu(OMNative.GetCurrentSlot() + 1)
    ElseIf aiAction == 8
        _SetSlotFromMenu(aiSlot)
    EndIf
EndFunction

Function _PollOpenMenu() Global
    Int iGuard = 0
    While iGuard < 18000 && (OMNative.IsMenuOpen() || OMNative.GetMenuAction() != 0)
        Int iAction = OMNative.GetMenuAction()
        If iAction != 0
            Int iSlot = OMNative.GetMenuActionSlot()
            OMNative.ClearMenuAction()
            _HandleMenuAction(iAction, iSlot)
        EndIf
        Utility.Wait(_MenuPollSeconds())
        iGuard += 1
    EndWhile
    OMNative.ClearMenuTarget()
    If !OMNative.IsMenuOpen()
        _RestoreThirdPersonAfterClose()
    EndIf
EndFunction

Function _RestoreThirdPersonAfterClose() Global
    Utility.Wait(0.10)
    Game.ForceThirdPerson()
    ; Keep the third-person camera, but make the first-person geometry
    ; available for the next Pip-Boy or tactical-tablet open.
    Game.ShowFirstPersonGeometry(True)
EndFunction

Function _PreparePlayerPreview() Global
    If !OMNative.PreparePlayerPreview()
        Return
    EndIf

    ; The first-person body and the third-person body use different render
    ; paths. Keep both resources rendered while selecting the third-person
    ; animation graph, so a later Pip-Boy or tactical tablet remains intact.
    Game.ForceThirdPerson()
    GardenOfEden3.ShowFirstPersonBody(False)
    GardenOfEden3.RenderPlayerBody(True, True)
    Utility.Wait(0.10)
EndFunction

Function OnMcmMenuHotkey() Global
    If OMNative.IsMenuOpen()
        OMNative.CloseMenu()
        OMNative.ClearMenuTarget()
        _RestoreThirdPersonAfterClose()
        Return
    EndIf

    Actor akTarget = _GetTargetActor()
    String sTargetName = _GetTargetName(akTarget)

    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("菜单冷却中。")
        Return
    EndIf

    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.MessageBox("服装管理器 UI 暂未连接。\n\n目标：" + sTargetName + "\n当前槽位：" + OMNative.GetCurrentSlot() + "\n\n请确认 Prisma UI Framework 已安装，并使用带 Prisma 接入的 OutfitManager.dll。")
        Return
    EndIf

    _PreparePlayerPreview()
    If OMNative.OpenMenu(akTarget)
        _NotifyOutfitInfo("已打开服装管理器：" + sTargetName)
        _PollOpenMenu()
        Return
    EndIf

    OMNative.ClearMenuTarget()
    Debug.Notification("界面正在准备中，请稍后再试。")
EndFunction

Function OnMcmSaveHotkey() Global
    Actor akTarget = _ResolveMenuActionTarget(None)
    _LogTarget("SaveHotkey resolved", akTarget)

    If _IsPowerArmorBlocked(akTarget)
        Return
    EndIf

    Int iSlot = OMNative.GetActiveOutfitSlot(akTarget)
    If iSlot <= 0
        iSlot = OMNative.FindFirstEmptyOutfitSlot()
    EndIf
    If iSlot <= 0
        Debug.Notification("保存失败：没有可用的空套装。")
        Return
    EndIf

    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("保存功能冷却中。")
        Return
    EndIf

    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.Notification("Prisma UI Framework 暂未连接。")
        Return
    EndIf

    _PreparePlayerPreview()
    If OMNative.OpenQuickSaveMenu(akTarget, iSlot)
        _PollOpenMenu()
        Return
    EndIf
    OMNative.ClearMenuTarget()
    Debug.Notification("保存界面暂时无法打开。")
EndFunction

Function OnMcmLoadHotkey() Global
    If OMNative.IsEquipBusy()
        Debug.Notification("正在换装，请稍候。")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetEquipCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("换装冷却中。")
        Return
    EndIf

    Actor akTarget = _ResolveMenuActionTarget(None)
    String sTargetName = _GetTargetName(akTarget)
    _LogTarget("LoadHotkey resolved", akTarget)

    If _IsPowerArmorBlocked(akTarget)
        Return
    EndIf

    Int iSlot = OMNative.GetActiveOutfitSlot(akTarget)
    If iSlot <= 0 || !OMNative.HasSlotData(iSlot)
        Debug.Notification("没有当前活跃套装，请先确认穿戴一套服装。")
        Return
    EndIf

    Int iSlotGender = OMNative.GetSlotGender(iSlot)
    Int iTargetSex = _GetSex(akTarget)
    If iSlotGender == -1 || iTargetSex == -1
        Debug.Notification("装备失败：无法确认目标性别。")
        Return
    EndIf

    If iSlotGender != iTargetSex
        Debug.Notification("装备失败：当前套装与目标性别不匹配。")
        Return
    EndIf

    Int iResult = OMNative.LoadOutfit(iSlot, akTarget)
    If iResult > 0 && OMNative.IsWearingOutfitNative(iSlot, akTarget)
        Debug.Notification("已经穿着：" + OMNative.GetSavedOutfitName(iSlot))
        Return
    EndIf

    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("换装冷却中。")
        Return
    EndIf

    Utility.Wait(_RestorePrepareSeconds())

    iResult = OMNative.EquipOutfitNative(iSlot, akTarget)
    If iResult == -5 || iResult == -8
        OMNative.FinishEquipAction()
        Return
    ElseIf iResult == -6
        OMNative.FinishEquipAction()
        Debug.Notification("战斗中不能换装")
        Return
    ElseIf iResult == -7
        OMNative.FinishEquipAction()
        Debug.Notification("目标正由任务或特殊状态控制，暂时不能换装")
        Return
    EndIf
    If iResult > 0
        OMNative.FinishEquipAction()
        Debug.Notification("已为" + sTargetName + "装备：" + OMNative.GetSavedOutfitName(iSlot))
    ElseIf iResult == -2
        OMNative.FinishEquipAction()
        Debug.Notification("装备失败：套装与目标性别不匹配。")
    ElseIf iResult == -1
        OMNative.FinishEquipAction()
        Debug.Notification("装备失败：当前活跃套装无法读取。")
    Else
        OMNative.FinishEquipAction()
        Debug.Notification("装备当前套装失败。")
    EndIf
EndFunction

Function OnMcmRandomHotkey() Global
    If OMNative.IsEquipBusy()
        Debug.Notification("正在换装，请稍候。")
        Return
    EndIf

    If OMNative.IsRandomBusy()
        Debug.Notification("正在随机换装，请稍候。")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetRandomCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("随机换装冷却中。")
        Return
    EndIf

    Actor akTarget = _ResolveMenuActionTarget(None)
    String sTargetName = _GetTargetName(akTarget)
    _LogTarget("RandomHotkey resolved", akTarget)
    Int iTargetSex = _GetSex(akTarget)

    If _IsPowerArmorBlocked(akTarget)
        Return
    EndIf

    If iTargetSex == -1
        Debug.Notification("随机换装失败：无法确认目标性别。")
        Return
    EndIf

    Int iLastRandomSlot = OMNative.GetLastRandomSlot()
    Int iActiveSlot = OMNative.GetActiveOutfitSlot(akTarget)
    Int iChosenSlot = OMNative.ChooseRandomSlot(iTargetSex, iActiveSlot, iLastRandomSlot)

    If iChosenSlot == 0
        Debug.Notification("随机换装失败：没有符合目标性别的套装。")
        Return
    EndIf

    If !OMNative.BeginRandom(Utility.GetCurrentRealTime(), _RandomCooldownSeconds())
        Debug.Notification("随机换装冷却中。")
        Return
    EndIf

    Utility.Wait(_RandomPrepareSeconds())

    Int iFirstSlot = iChosenSlot
    Int iAttempts = 0
    Int iResult = 0
    While iChosenSlot > 0 && iAttempts < _MaxOutfitSlots()
        iResult = OMNative.EquipOutfitNative(iChosenSlot, akTarget)
        If iResult == -5 || iResult == -8
            OMNative.FinishRandom(0)
            Return
        ElseIf iResult == -6
            OMNative.FinishRandom(0)
            Debug.Notification("战斗中不能换装")
            Return
        ElseIf iResult == -7
            OMNative.FinishRandom(0)
            Debug.Notification("目标正由任务或特殊状态控制，暂时不能换装")
            Return
        EndIf
        If iResult > 0
            OMNative.FinishRandom(iChosenSlot)
            Debug.Notification("已为" + sTargetName + "随机换装：" + OMNative.GetSavedOutfitName(iChosenSlot))
            Return
        EndIf

        iAttempts += 1
        Int iNextSlot = OMNative.ChooseRandomSlot(iTargetSex, iActiveSlot, iChosenSlot)
        If iNextSlot == 0 || iNextSlot == iFirstSlot
            iChosenSlot = 0
        Else
            iChosenSlot = iNextSlot
        EndIf
    EndWhile

    If iActiveSlot > 0 && OMNative.HasSlotData(iActiveSlot)
        OMNative.EquipOutfitNative(iActiveSlot, akTarget)
    EndIf
    OMNative.FinishRandom(0)
    Debug.Notification("随机换装失败：没有可用的已保存套装。")
EndFunction

Int Function _FindMatchingOutfitSlot(Int aiCurrentSlot, Int aiDirection, Int aiTargetSex) Global
    Return OMNative.FindMatchingSlot(aiCurrentSlot, aiDirection, aiTargetSex)
EndFunction

Function _RestoreNearestMatchingOutfit(Int aiDirection) Global
    If OMNative.IsEquipBusy()
        Debug.Notification("正在换装，请稍候。")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetEquipCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("换装冷却中。")
        Return
    EndIf

    Actor akTarget = _GetTargetActor()
    String sTargetName = _GetTargetName(akTarget)
    Int iTargetSex = _GetSex(akTarget)

    If _IsPowerArmorBlocked(akTarget)
        Return
    EndIf

    If iTargetSex == -1
        Debug.Notification("装备失败：无法确认目标性别。")
        Return
    EndIf

    Int iCurrentSlot = OMNative.GetActiveOutfitSlot(akTarget)
    Int iChosenSlot = _FindMatchingOutfitSlot(iCurrentSlot, aiDirection, iTargetSex)

    If iChosenSlot == 0
        Debug.Notification("装备失败：没有符合目标性别的已保存套装。")
        Return
    EndIf

    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("换装冷却中。")
        Return
    EndIf

    Utility.Wait(_RestorePrepareSeconds())

    Int iFirstSlot = iChosenSlot
    Int iAttempts = 0
    Int iResult = 0
    While iChosenSlot > 0 && iAttempts < _MaxOutfitSlots()
        iResult = OMNative.EquipOutfitNative(iChosenSlot, akTarget)
        If iResult == -5 || iResult == -8
            OMNative.FinishEquipAction()
            Return
        ElseIf iResult == -6
            OMNative.FinishEquipAction()
            Debug.Notification("战斗中不能换装")
            Return
        ElseIf iResult == -7
            OMNative.FinishEquipAction()
            Debug.Notification("目标正由任务或特殊状态控制，暂时不能换装")
            Return
        EndIf
        If iResult > 0
            OMNative.FinishEquipAction()
            Debug.Notification("已为" + sTargetName + "装备：" + OMNative.GetSavedOutfitName(iChosenSlot))
            Return
        EndIf

        iAttempts += 1
        Int iNextSlot = _FindMatchingOutfitSlot(iChosenSlot, aiDirection, iTargetSex)
        If iNextSlot == 0 || iNextSlot == iFirstSlot
            iChosenSlot = 0
        Else
            iChosenSlot = iNextSlot
        EndIf
    EndWhile

    If iCurrentSlot > 0 && OMNative.HasSlotData(iCurrentSlot)
        OMNative.EquipOutfitNative(iCurrentSlot, akTarget)
    EndIf
    OMNative.FinishEquipAction()
    Debug.Notification("装备失败：没有可用的已保存套装。")
EndFunction

Function OnMcmNextOutfitHotkey() Global
    _RestoreNearestMatchingOutfit(1)
EndFunction

Function OnMcmPreviousOutfitHotkey() Global
    _RestoreNearestMatchingOutfit(-1)
EndFunction
Function OnMcmCycleSlotHotkey() Global
    Int iCurrentSlot = OMNative.GetCurrentSlot()
    Int iNewSlot = iCurrentSlot + 1
    If iNewSlot > _MaxOutfitSlots()
        iNewSlot = 1
    EndIf

    OMNative.SetCurrentSlot(iNewSlot)
    Debug.MessageBox(_SlotPreviewText(iNewSlot))
EndFunction

Function OnMcmPreviousSlotHotkey() Global
    Int iCurrentSlot = OMNative.GetCurrentSlot()
    Int iNewSlot = iCurrentSlot - 1
    If iNewSlot < 1
        iNewSlot = _MaxOutfitSlots()
    EndIf

    OMNative.SetCurrentSlot(iNewSlot)
    Debug.MessageBox(_SlotPreviewText(iNewSlot))
EndFunction
