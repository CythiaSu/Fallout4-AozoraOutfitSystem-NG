ScriptName OMControllerQuestScript Extends Quest

Form Property ControllerItem Auto

Event OnInit()
    GiveController()
EndEvent

Function GiveController()
    Actor akPlayer = Game.GetPlayer()
    If akPlayer != None && ControllerItem != None && akPlayer.GetItemCount(ControllerItem) <= 0
        akPlayer.AddItem(ControllerItem, 1, True)
    EndIf
EndFunction
