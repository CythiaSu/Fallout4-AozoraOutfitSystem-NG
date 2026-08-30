ScriptName OMControllerEffectScript Extends ActiveMagicEffect

Form Property ControllerItem Auto

Event OnEffectStart(Actor akTarget, Actor akCaster)
    If UI.IsMenuOpen("PipboyMenu")
        UI.CloseMenu("PipboyMenu")
        Int iGuard = 0
        While iGuard < 20 && UI.IsMenuOpen("PipboyMenu")
            Utility.Wait(0.05)
            iGuard += 1
        EndWhile
    EndIf
    Utility.Wait(0.10)
    OutfitManager.OnMcmMenuHotkey()

    Actor akPlayer = Game.GetPlayer()
    If akPlayer != None && ControllerItem != None && akPlayer.GetItemCount(ControllerItem) <= 0
        akPlayer.AddItem(ControllerItem, 1, True)
    EndIf
EndEvent
