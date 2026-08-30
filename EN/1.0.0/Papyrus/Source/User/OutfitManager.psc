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

Actor Function _GetQuickRayNpcTarget() Global
    ObjectReference akRef = GardenOfEden3.GetCameraTargetReference()
    Actor akHit = akRef as Actor
    If akHit != None && OMNative.IsValidOutfitTarget(akHit)
        Return akHit
    EndIf
    Return None
EndFunction

Actor Function _GetQuickOutfitTarget() Global
    Actor akNpc = _GetQuickRayNpcTarget()
    If akNpc != None
        Return akNpc
    EndIf
    Return Game.GetPlayer()
EndFunction

Function EnsureControllerItems() Global
    Actor akPlayer = Game.GetPlayer()
    If akPlayer == None
        Return
    EndIf

    Form kMainController = Game.GetFormFromFile(0x00000801, "OutfitManager.esp")
    Form kStudioController = Game.GetFormFromFile(0x00000807, "OutfitManager.esp")
    Form kQuickController = Game.GetFormFromFile(0x00000809, "OutfitManager.esp")
    If kMainController != None && akPlayer.GetItemCount(kMainController) <= 0
        akPlayer.AddItem(kMainController, 1, True)
    EndIf
    If kStudioController != None && akPlayer.GetItemCount(kStudioController) <= 0
        akPlayer.AddItem(kStudioController, 1, True)
    EndIf
    If kQuickController != None && akPlayer.GetItemCount(kQuickController) <= 0
        akPlayer.AddItem(kQuickController, 1, True)
    EndIf
EndFunction

Function _StartQuickTargetEffect(Actor akTarget) Global
    If akTarget == None
        Return
    EndIf
    If OMNative.StartQuickTargetEffect(akTarget)
        Return
    EndIf
    EffectShader TargetShader = Game.GetForm(0x001E077C) as EffectShader
    If TargetShader != None
        TargetShader.Play(akTarget, -1.0)
    EndIf
EndFunction

Function _StopQuickTargetEffect(Actor akTarget) Global
    If akTarget == None
        Return
    EndIf
    EffectShader TargetShader = Game.GetForm(0x001E077C) as EffectShader
    If TargetShader != None
        TargetShader.Stop(akTarget)
    EndIf
EndFunction

Function _FinishQuickTargetEffect(Actor akTarget) Global
    Utility.Wait(0.08)
    _StopQuickTargetEffect(akTarget)
    OMNative.RestoreQuickTargetEffect()
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
        Return "Player"
    EndIf

    String sName = OMNative.GetActorName(akTarget)
    If sName == ""
        Return "Unknown NPC"
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
        Return "Male"
    ElseIf aiSex == 1
        Return "Female"
    EndIf
    Return "Unknown"
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
        Debug.Notification("Outfits cannot be saved or equipped while using Power Armor. Exit Power Armor first.")
        Return True
    EndIf
    Return False
EndFunction

String Function _SlotPreviewText(Int aiSlot) Global
    String sStatus = "Empty"
    String sDetails = ""

    If OMNative.HasSlotData(aiSlot)
        sStatus = "Saved"
        sDetails = "\nSex：" + _SexText(OMNative.GetSlotGender(aiSlot)) + "\nOutfit：" + OMNative.GetSavedOutfitSummary(aiSlot)
    EndIf

    Return "Active outfit slot: " + aiSlot + " (" + sStatus + ")" + sDetails
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
        Return "Equipped：0 / " + aiCount
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
    Return "Equipped：" + iEquipped + " / " + aiCount
EndFunction

Function _ResetNpcOutfitFromMenu(Actor akForcedTarget = None) Global
    Actor akTarget = _ResolveMenuActionTarget(akForcedTarget)
    _LogTarget("ResetNpc resolved", akTarget)
    If akTarget == None || akTarget == Game.GetPlayer()
        Debug.Notification("NPC outfit reset requires a valid NPC target.")
        Return
    EndIf

    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("This action is on cooldown.")
        Return
    EndIf

    Int iDefaultCount = OMNative.GetDefaultOutfitCount(akTarget)
    If iDefaultCount <= 0
        Debug.Notification("No default outfit was recorded for this NPC.")
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
    _NotifyOutfitInfo("NPC default outfit restored.")
EndFunction

Function _ClearSlotFromMenu(Int aiSlot) Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("This action is on cooldown.")
        Return
    EndIf

    Int iSlot = aiSlot
    If iSlot < 1 || iSlot > _MaxOutfitSlots()
        iSlot = OMNative.GetCurrentSlot()
    EndIf

    If OMNative.ClearSlotData(iSlot)
        _NotifyOutfitInfo("Slot " + iSlot + " cleared.")
    Else
        Debug.Notification("Failed to clear the slot.")
    EndIf
EndFunction

Function _SetSlotFromMenu(Int aiSlot) Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), 0.2)
        Debug.Notification("This action is on cooldown.")
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
        Debug.MessageBox("Save failed.\nThe target is using Power Armor.\n\nExit Power Armor before saving a standard outfit.")
        Return
    EndIf
    Int iResult = OMNative.SaveMenuActionTargetOutfit(aiSlot)
    If iResult > 0
        Debug.MessageBox("Outfit Saved。\nTarget：" + sTargetName + "\nSlot：" + aiSlot + "\nItems：" + iResult + "\n\nOutfit：" + OMNative.GetSavedOutfitSummary(aiSlot))
    Else
        Debug.MessageBox("Save failed.\nTarget: " + sTargetName + "\nSlot: " + aiSlot + "\nNo valid clothing was detected.")
    EndIf
EndFunction

Function _LoadSelectedMenuTarget(Int aiSlot) Global
    If OMNative.IsEquipBusy()
        Debug.Notification("Changing outfit.")
        Return
    EndIf
    If OMNative.GetEquipCooldownRemaining(Utility.GetCurrentRealTime()) > 0.0
        Debug.Notification("Restore is on cooldown.")
        Return
    EndIf
    If !OMNative.HasSlotData(aiSlot)
        Debug.Notification("Restore failed.\nSlot " + aiSlot + " is empty.")
        Return
    EndIf
    If OMNative.IsMenuActionTargetPowerArmorBlocked()
        Return
    EndIf
    Int iResult = OMNative.LoadMenuActionTargetOutfit(aiSlot)
    If iResult > 0 && OMNative.IsMenuActionTargetWearingOutfit(aiSlot)
        Debug.Notification("This outfit is already equipped.")
        Return
    EndIf
    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("Restore is on cooldown.")
        Return
    EndIf
    _NotifyOutfitInfo("Restoring outfit...")
    Utility.Wait(_RestorePrepareSeconds())
    iResult = OMNative.EquipMenuActionTargetOutfit(aiSlot)
    If iResult == -5 || iResult == -8
        OMNative.FinishEquipAction()
        Return
    ElseIf iResult == -6
        OMNative.FinishEquipAction()
        Debug.Notification("Outfits cannot be changed during combat")
        Return
    ElseIf iResult == -7
        OMNative.FinishEquipAction()
        Debug.Notification("The target is controlled by a quest or special state and cannot change outfits now")
        Return
    EndIf
    If iResult > 0
        OMNative.SetLastEquippedSlot(aiSlot)
        OMNative.FinishEquipAction()
        _NotifyOutfitInfo("Outfit restored.\nTarget: " + OMNative.GetMenuActionTargetName() + "\nSlot: " + aiSlot + "\nEquipped: " + iResult + "\n\nOutfit: " + OMNative.GetSavedOutfitSummary(aiSlot))
    Else
        OMNative.FinishEquipAction()
        Debug.Notification("Restore failed.\nSlot: " + aiSlot)
    EndIf
EndFunction

Function _RandomSelectedMenuTarget() Global
    If OMNative.IsEquipBusy() || OMNative.IsRandomBusy()
        Debug.Notification("Restoring a random outfit.")
        Return
    EndIf
    If OMNative.GetRandomCooldownRemaining(Utility.GetCurrentRealTime()) > 0.0
        Debug.Notification("Random action is on cooldown.")
        Return
    EndIf
    If OMNative.IsMenuActionTargetPowerArmorBlocked()
        Return
    EndIf
    Int iTargetSex = OMNative.GetMenuActionTargetSex()
    Int iChosenSlot = OMNative.ChooseRandomSlot(iTargetSex, 0, OMNative.GetLastRandomSlot())
    If iChosenSlot == 0
        Debug.Notification("Random restore failed.\nNo saved outfit matches target: " + OMNative.GetMenuActionTargetName())
        Return
    EndIf
    If !OMNative.BeginRandom(Utility.GetCurrentRealTime(), _RandomCooldownSeconds())
        Debug.Notification("Random action is on cooldown.")
        Return
    EndIf
    _NotifyOutfitInfo("Choosing an outfit...")
    Utility.Wait(_RandomPrepareSeconds())
    Int iResult = OMNative.EquipMenuActionTargetOutfit(iChosenSlot)
    If iResult == -5 || iResult == -8
        OMNative.FinishRandom(0)
        Return
    ElseIf iResult == -6
        OMNative.FinishRandom(0)
        Debug.Notification("Outfits cannot be changed during combat")
        Return
    ElseIf iResult == -7
        OMNative.FinishRandom(0)
        Debug.Notification("The target is controlled by a quest or special state and cannot change outfits now")
        Return
    EndIf
    If iResult > 0
        OMNative.SetLastEquippedSlot(iChosenSlot)
        OMNative.FinishRandom(iChosenSlot)
        _NotifyOutfitInfo("Random outfit restored.\nTarget: " + OMNative.GetMenuActionTargetName() + "\nSlot: " + iChosenSlot + "\nEquipped: " + iResult + "\n\nOutfit: " + OMNative.GetSavedOutfitSummary(iChosenSlot))
    Else
        OMNative.FinishRandom(0)
        Debug.Notification("Random restore failed.\nSlot: " + iChosenSlot)
    EndIf
EndFunction

Function _ResetSelectedMenuTarget() Global
    If !OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
        Debug.Notification("This action is on cooldown.")
        Return
    EndIf
    Int iResult = OMNative.ResetMenuActionTargetOutfit()
    If iResult > 0
        _NotifyOutfitInfo("Target outfit reset.")
    Else
        Debug.Notification("No default outfit or managed items were recorded for this target。")
    EndIf
EndFunction

Function _HandleMenuAction(Int aiAction, Int aiSlot) Global
    If aiAction == 1
        If OMNative.BeginMenuAction(Utility.GetCurrentRealTime(), _MenuActionCooldownSeconds())
            OMNative.SetCurrentSlot(aiSlot)
            _SaveSelectedMenuTarget(aiSlot)
            OMNative.CloseMenu()
        Else
            Debug.Notification("This action is on cooldown.")
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

Function _PollOpenMenu(Bool abRestoreThirdPerson, Actor akQuickFeedbackTarget) Global
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
    _StopQuickTargetEffect(akQuickFeedbackTarget)
    If akQuickFeedbackTarget != None
        _FinishQuickTargetEffect(akQuickFeedbackTarget)
    EndIf
    OMNative.ClearMenuTarget()
    If !OMNative.IsMenuOpen() && abRestoreThirdPerson
        _RestoreThirdPersonAfterClose()
    EndIf
EndFunction

Function _RestoreThirdPersonAfterClose() Global
    Utility.Wait(0.10)
    ; The Pip-Boy can open during this short close hand-off.  Do not force a
    ; camera state after it has taken ownership, or its transition can remain
    ; half-open with only the bottom action bar visible.
    If UI.IsMenuOpen("PipboyMenu") || UI.IsMenuOpen("WorkshopMenu") || UI.IsMenuOpen("DialogueMenu")
        Game.ShowFirstPersonGeometry(True)
        Return
    EndIf
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
    EnsureControllerItems()
    If OMNative.IsMenuOpen()
        OMNative.CloseMenu()
        OMNative.ClearMenuTarget()
        _RestoreThirdPersonAfterClose()
        Return
    EndIf

    Actor akTarget = _GetTargetActor()
    String sTargetName = _GetTargetName(akTarget)

    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("The menu is on cooldown.")
        Return
    EndIf

    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.MessageBox("The outfit manager UI is not connected.\n\nTarget: " + sTargetName + "\nActive slot: " + OMNative.GetCurrentSlot() + "\n\nConfirm that Prisma UI Framework is installed and that the Prisma-enabled OutfitManager.dll is in use.")
        Return
    EndIf

    _PreparePlayerPreview()
    If OMNative.OpenMenu(akTarget)
        _NotifyOutfitInfo("Outfit management opened for: " + sTargetName)
        _PollOpenMenu(True, None)
        Return
    EndIf

    OMNative.ClearMenuTarget()
    Debug.Notification("The interface is preparing. Try again shortly.")
EndFunction

Function OnMcmStudioHotkey() Global
    EnsureControllerItems()
    If OMNative.IsMenuOpen()
        OMNative.CloseMenu()
        OMNative.ClearMenuTarget()
        _RestoreThirdPersonAfterClose()
        Return
    EndIf

    Actor akTarget = _GetTargetActor()
    String sTargetName = _GetTargetName(akTarget)
    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("The Outfit Studio is on cooldown.")
        Return
    EndIf
    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.Notification("The Outfit Studio UI is not connected.")
        Return
    EndIf

    _PreparePlayerPreview()
    If OMNative.OpenStudioMenu(akTarget)
        _NotifyOutfitInfo("Outfit Studio opened for: " + sTargetName)
        _PollOpenMenu(True, None)
        Return
    EndIf

    OMNative.ClearMenuTarget()
    Debug.Notification("The Outfit Studio cannot be opened right now.")
EndFunction

Function OnMcmQuickOutfitHotkey() Global
    EnsureControllerItems()
    If OMNative.IsMenuOpen()
        Actor akCloseTarget = OMNative.GetMenuTarget()
        OMNative.CloseMenu()
        _StopQuickTargetEffect(akCloseTarget)
        If akCloseTarget != None
            _FinishQuickTargetEffect(akCloseTarget)
        EndIf
        OMNative.ClearMenuTarget()
        Return
    EndIf

    Actor akTarget = _GetQuickOutfitTarget()
    Actor akFeedbackTarget = _GetQuickRayNpcTarget()
    String sTargetName = _GetTargetName(akTarget)

    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("Quick outfit menu is on cooldown.")
        Return
    EndIf

    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.MessageBox("The Outfit Manager UI is not connected.\n\nTarget: " + sTargetName + "\n\nPlease confirm that Prisma UI Framework is installed and that the OutfitManager.dll build includes Prisma integration.")
        Return
    EndIf

    If akFeedbackTarget != None
        _StartQuickTargetEffect(akFeedbackTarget)
        Utility.Wait(0.05)
    EndIf

    If OMNative.OpenQuickOutfitMenu(akTarget)
        _NotifyOutfitInfo("Quick outfit menu opened: " + sTargetName)
        _PollOpenMenu(False, akFeedbackTarget)
        Return
    EndIf

    _StopQuickTargetEffect(akFeedbackTarget)
    If akFeedbackTarget != None
        _FinishQuickTargetEffect(akFeedbackTarget)
    EndIf
    OMNative.ClearMenuTarget()
    Debug.Notification("The quick outfit menu could not be opened.")
EndFunction

Function OnMcmResetUsageCounts() Global
    Int iReset = OMNative.ResetOutfitUsageCounts()
    If iReset > 0
        Debug.Notification("Outfit usage counts reset: " + iReset)
    Else
        Debug.Notification("All outfit usage counts are already zero.")
    EndIf
EndFunction

Function ResetUsageCountsFromMcm()
    OnMcmResetUsageCounts()
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
        Debug.Notification("Save failed: no empty outfit slot is available.")
        Return
    EndIf

    If !OMNative.BeginMenuOpen(Utility.GetCurrentRealTime(), _MenuOpenCooldownSeconds())
        Debug.Notification("Save is on cooldown.")
        Return
    EndIf

    If !OMNative.IsMenuAvailable()
        OMNative.ClearMenuTarget()
        Debug.Notification("Prisma UI Framework is not connected.")
        Return
    EndIf

    _PreparePlayerPreview()
    If OMNative.OpenQuickSaveMenu(akTarget, iSlot)
        _PollOpenMenu(True, None)
        Return
    EndIf
    OMNative.ClearMenuTarget()
    Debug.Notification("The save interface cannot be opened right now.")
EndFunction

Function OnMcmLoadHotkey() Global
    If OMNative.IsEquipBusy()
        Debug.Notification("Changing outfit. Please wait.")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetEquipCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("Outfit change is on cooldown.")
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
        Debug.Notification("No active outfit. Equip an outfit first.")
        Return
    EndIf

    Int iSlotGender = OMNative.GetSlotGender(iSlot)
    Int iTargetSex = _GetSex(akTarget)
    If iSlotGender == -1 || iTargetSex == -1
        Debug.Notification("Equip failed: unable to determine target sex.")
        Return
    EndIf

    If iSlotGender != iTargetSex
        Debug.Notification("Equip failed: the active outfit does not match the target.")
        Return
    EndIf

    Int iResult = OMNative.LoadOutfit(iSlot, akTarget)
    If iResult > 0 && OMNative.IsWearingOutfitNative(iSlot, akTarget)
        Debug.Notification("Already equipped: " + OMNative.GetSavedOutfitName(iSlot))
        Return
    EndIf

    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("Outfit change is on cooldown.")
        Return
    EndIf

    Utility.Wait(_RestorePrepareSeconds())

    iResult = OMNative.EquipOutfitNative(iSlot, akTarget)
    If iResult == -5 || iResult == -8
        OMNative.FinishEquipAction()
        Return
    ElseIf iResult == -6
        OMNative.FinishEquipAction()
        Debug.Notification("Outfits cannot be changed during combat")
        Return
    ElseIf iResult == -7
        OMNative.FinishEquipAction()
        Debug.Notification("The target is controlled by a quest or special state and cannot change outfits now")
        Return
    EndIf
    If iResult > 0
        OMNative.FinishEquipAction()
        Debug.Notification("Equipped for " + sTargetName + ": " + OMNative.GetSavedOutfitName(iSlot))
    ElseIf iResult == -2
        OMNative.FinishEquipAction()
        Debug.Notification("Equip failed: the outfit does not match the target.")
    ElseIf iResult == -1
        OMNative.FinishEquipAction()
        Debug.Notification("Equip failed: the active outfit could not be read.")
    Else
        OMNative.FinishEquipAction()
        Debug.Notification("Failed to equip the active outfit.")
    EndIf
EndFunction

Function OnMcmRandomHotkey() Global
    If OMNative.IsEquipBusy()
        Debug.Notification("Changing outfit. Please wait.")
        Return
    EndIf

    If OMNative.IsRandomBusy()
        Debug.Notification("Choosing a random outfit. Please wait.")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetRandomCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("Random outfit is on cooldown.")
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
        Debug.Notification("Random outfit failed: unable to determine target sex.")
        Return
    EndIf

    Int iLastRandomSlot = OMNative.GetLastRandomSlot()
    Int iActiveSlot = OMNative.GetActiveOutfitSlot(akTarget)
    Int iChosenSlot = OMNative.ChooseRandomSlot(iTargetSex, iActiveSlot, iLastRandomSlot)

    If iChosenSlot == 0
        Debug.Notification("Random outfit failed: no saved outfit matches the target.")
        Return
    EndIf

    If !OMNative.BeginRandom(Utility.GetCurrentRealTime(), _RandomCooldownSeconds())
        Debug.Notification("Random outfit is on cooldown.")
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
            Debug.Notification("Outfits cannot be changed during combat")
            Return
        ElseIf iResult == -7
            OMNative.FinishRandom(0)
            Debug.Notification("The target is controlled by a quest or special state and cannot change outfits now")
            Return
        EndIf
        If iResult > 0
            OMNative.FinishRandom(iChosenSlot)
            Debug.Notification("Random outfit equipped for " + sTargetName + ": " + OMNative.GetSavedOutfitName(iChosenSlot))
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
    Debug.Notification("Random outfit failed: no usable saved outfit is available.")
EndFunction

Int Function _FindMatchingOutfitSlot(Int aiCurrentSlot, Int aiDirection, Int aiTargetSex) Global
    Return OMNative.FindMatchingSlot(aiCurrentSlot, aiDirection, aiTargetSex)
EndFunction

Function _RestoreNearestMatchingOutfit(Int aiDirection) Global
    If OMNative.IsEquipBusy()
        Debug.Notification("Changing outfit. Please wait.")
        Return
    EndIf

    Float fNow = Utility.GetCurrentRealTime()
    Float fRemaining = OMNative.GetEquipCooldownRemaining(fNow)
    If fRemaining > 0.0
        Debug.Notification("Outfit change is on cooldown.")
        Return
    EndIf

    Actor akTarget = _GetTargetActor()
    String sTargetName = _GetTargetName(akTarget)
    Int iTargetSex = _GetSex(akTarget)

    If _IsPowerArmorBlocked(akTarget)
        Return
    EndIf

    If iTargetSex == -1
        Debug.Notification("Equip failed: unable to determine target sex.")
        Return
    EndIf

    Int iCurrentSlot = OMNative.GetActiveOutfitSlot(akTarget)
    Int iChosenSlot = _FindMatchingOutfitSlot(iCurrentSlot, aiDirection, iTargetSex)

    If iChosenSlot == 0
        Debug.Notification("Equip Failed：No saved outfit matches the target.")
        Return
    EndIf

    If !OMNative.BeginEquipAction(Utility.GetCurrentRealTime(), _RestoreCooldownSeconds())
        Debug.Notification("Outfit change is on cooldown.")
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
            Debug.Notification("Outfits cannot be changed during combat")
            Return
        ElseIf iResult == -7
            OMNative.FinishEquipAction()
            Debug.Notification("The target is controlled by a quest or special state and cannot change outfits now")
            Return
        EndIf
        If iResult > 0
            OMNative.FinishEquipAction()
            Debug.Notification("Equipped for " + sTargetName + ": " + OMNative.GetSavedOutfitName(iChosenSlot))
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
    Debug.Notification("Equip Failed：No usable saved outfit is available.")
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
