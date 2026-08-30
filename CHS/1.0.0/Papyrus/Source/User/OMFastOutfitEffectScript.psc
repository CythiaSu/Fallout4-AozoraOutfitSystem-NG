ScriptName OMFastOutfitEffectScript Extends ActiveMagicEffect

Form Property ControllerItem Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    ; Allow the Pip-Boy close animation to finish before opening the menu.
    Actor akPlayer = Game.GetPlayer()
    UI.CloseMenu("PipboyMenu")
    Utility.WaitMenuMode(0.80)
    Bool bCameraReady = False
    Int iGuard = 0
    While iGuard < 40 && !bCameraReady
        Int iCameraState = Game.GetCameraState()
        bCameraReady = iCameraState == 0 || iCameraState == 7 || iCameraState == 8
        If bCameraReady
            Utility.WaitMenuMode(0.12)
            iCameraState = Game.GetCameraState()
            bCameraReady = iCameraState == 0 || iCameraState == 7 || iCameraState == 8
        Else
            Utility.WaitMenuMode(0.05)
        EndIf
        iGuard += 1
    EndWhile
    Utility.WaitMenuMode(0.05)
    OutfitManager.OnMcmQuickOutfitHotkey()

    RestoreControllerItem(akPlayer)
EndEvent

Function RestoreControllerItem(Actor akPlayer)
    If akPlayer != None && ControllerItem != None && akPlayer.GetItemCount(ControllerItem) <= 0
        akPlayer.AddItem(ControllerItem, 1, True)
    EndIf
EndFunction
