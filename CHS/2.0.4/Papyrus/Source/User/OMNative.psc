ScriptName OMNative Hidden

String Function GetPluginVersion() Global Native
String Function GetSlotPath(Int aiSlot) Global Native
Bool Function IsMenuAvailable() Global Native
Bool Function PreparePlayerPreview() Global Native
Bool Function OpenMenu(Actor akTarget) Global Native
Bool Function OpenQuickSaveMenu(Actor akTarget, Int aiSlot) Global Native
Function CloseMenu() Global Native
Bool Function IsMenuOpen() Global Native
Actor Function GetMenuTarget() Global Native
Actor Function GetMenuActionTarget() Global Native
ObjectReference Function GetMenuActionTargetRef() Global Native
Bool Function HasMenuActionTarget() Global Native
Int Function GetMenuActionTargetSex() Global Native
String Function GetMenuActionTargetName() Global Native
Bool Function IsMenuActionTargetPowerArmorBlocked() Global Native
Int Function SaveMenuActionTargetOutfit(Int aiSlot) Global Native
Int Function LoadMenuActionTargetOutfit(Int aiSlot) Global Native
Bool Function IsMenuActionTargetWearingOutfit(Int aiSlot) Global Native
Int Function EquipMenuActionTargetOutfit(Int aiSlot) Global Native
Int Function ResetMenuActionTargetOutfit() Global Native
Bool Function IsWearingOutfitNative(Int aiSlot, Actor akTarget) Global Native
Int Function EquipOutfitNative(Int aiSlot, Actor akTarget) Global Native
Function ClearMenuTarget() Global Native
Float Function GetMenuOpenCooldownRemaining(Float afNow) Global Native
Bool Function BeginMenuOpen(Float afNow, Float afCooldownSeconds) Global Native
Float Function GetMenuActionCooldownRemaining(Float afNow) Global Native
Bool Function BeginMenuAction(Float afNow, Float afCooldownSeconds) Global Native
Int Function GetMenuAction() Global Native
Int Function GetMenuActionSlot() Global Native
Function ClearMenuAction() Global Native

Actor Function GetCameraTarget() Global Native
String Function GetActorName(Actor akTarget) Global Native
Function LogText(String asText) Global Native
Bool Function IsValidTarget(Actor akTarget) Global Native
Bool Function IsOutfitItem(Form akItem) Global Native
Bool Function IsPowerArmorItem(Form akItem) Global Native
Bool Function AreOutfitNotificationsEnabled() Global Native

Int Function GetCurrentSlot() Global Native
Function SetCurrentSlot(Int aiSlot) Global Native
Int Function GetActiveOutfitSlot(Actor akTarget) Global Native
Int Function FindFirstEmptyOutfitSlot() Global Native
Int Function GetLastRandomSlot() Global Native
Int Function GetLastEquippedSlot() Global Native
Function SetLastEquippedSlot(Int aiSlot) Global Native
Bool Function IsEquipBusy() Global Native
Float Function GetEquipCooldownRemaining(Float afNow) Global Native
Bool Function BeginEquipAction(Float afNow, Float afCooldownSeconds) Global Native
Function FinishEquipAction() Global Native
Bool Function IsRandomBusy() Global Native
Float Function GetRandomCooldownRemaining(Float afNow) Global Native
Bool Function BeginRandom(Float afNow, Float afCooldownSeconds) Global Native
Function FinishRandom(Int aiSlot) Global Native
Int Function ChooseRandomSlot(Int aiTargetSex, Int aiCurrentSlot, Int aiLastRandomSlot) Global Native
Int Function FindMatchingSlot(Int aiCurrentSlot, Int aiDirection, Int aiTargetSex) Global Native

Bool Function HasSlotData(Int aiSlot) Global Native
Int Function GetSlotGender(Int aiSlot) Global Native

Int Function SaveOutfit(Int aiSlot, Actor akTarget) Global Native
Bool Function BeginSaveOutfit(Int aiSlot, Int aiGender) Global Native
Bool Function AddSaveOutfitItem(Int aiSlot, Form akItem) Global Native
Int Function CommitSaveOutfit(Int aiSlot) Global Native
Int Function LoadOutfit(Int aiSlot, Actor akTarget) Global Native
Form Function GetSavedOutfitItem(Int aiSlot, Int aiIndex) Global Native
Int Function GetSavedOutfitItemID(Int aiSlot, Int aiIndex) Global Native
String Function GetSavedOutfitDebug(Int aiSlot) Global Native
String Function GetSavedOutfitSummary(Int aiSlot) Global Native
String Function GetSavedOutfitName(Int aiSlot) Global Native
Bool Function ClearSlotData(Int aiSlot) Global Native
Bool Function EnsureDefaultOutfitRecorded(Actor akTarget) Global Native
Bool Function RecordManagedItem(Actor akTarget, Form akItem) Global Native
Int Function GetManagedItemCount(Actor akTarget) Global Native
Form Function GetManagedItem(Actor akTarget, Int aiIndex) Global Native
Int Function GetManagedItemID(Actor akTarget, Int aiIndex) Global Native
Function ClearManagedItems(Actor akTarget) Global Native
Int Function GetDefaultOutfitCount(Actor akTarget) Global Native
Form Function GetDefaultOutfitItem(Actor akTarget, Int aiIndex) Global Native
Int Function GetDefaultOutfitItemID(Actor akTarget, Int aiIndex) Global Native

String Function DebugGetInventoryInfo(Actor akTarget) Global Native
Function RefreshActorModel(Actor akTarget) Global Native
