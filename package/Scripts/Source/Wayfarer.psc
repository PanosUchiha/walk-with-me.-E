Scriptname Wayfarer Hidden

Bool Function RegisterFollower(Actor akFollower, Int aiPreferredSlot = -1) Global Native
Bool Function UnregisterFollower(Actor akFollower) Global Native
Bool Function ExcludeFollower(Actor akFollower) Global Native
Bool Function IncludeFollower(Actor akFollower) Global Native
Bool Function IsManaged(Actor akFollower) Global Native

Function SetDialogueManagement(Actor akFollower, Bool abManage) Global Native
Function SetEnabled(Bool abEnabled) Global Native
Bool Function GetEnabled() Global Native
Function SetFormationMode(Int aiMode) Global Native
Int Function GetFormationMode() Global Native
Int Function GetManagedCount() Global Native
Function ReloadSettings() Global Native

Actor Function GetSlotFollower(Int aiSlot) Global Native
ObjectReference Function GetSlotMarker(Int aiSlot) Global Native
ObjectReference Function GetRestSeat(Int aiSlot) Global Native
Actor Function GetSocialTarget(Int aiSlot) Global Native
Bool Function HasSocialGroup() Global Native
Bool Function ShouldSuspendForDialogue() Global Native
Bool Function AliasSyncAllowed(Int ticket, Int session) Global Native
Function ReportAliasLook(Int slot, Actor actor, Actor target, Int ticket, Int session) Global Native
Function ReportAliasCleanup(Int slot, Bool pending, Int ticket, Int session) Global Native
Function ReportAliasSync(Bool abBusy) Global Native
Function ReportSocialIdle(Actor akActor, Bool abAccepted) Global Native
Idle Function ConsumeSocialIdle(Int aiSlot) Global Native

ObjectReference Function GetGatherMarker(Int aiSlot) Global Native
Idle Function ConsumeRestIdle(Int aiSlot) Global Native
Function ReportRestIdle(Int aiSlot, Actor akActor, Idle akIdle, Bool abAccepted) Global Native

Bool Function HasRestActivities() Global Native

; Ticketed handoff and serialized TETHER operations (revision 13).
Int Function GetTetherHandoffTicket(Int aiSlot) Global Native
ReferenceAlias Function GetTetherHandoffAlias(Int aiSlot, Int ticket) Global Native
Function ConfirmTetherHandoff(Int aiSlot, Int ticket) Global Native
Int Function TetherStepStatus(Int ticket, Int operation, Actor leader, Actor follower) Global Native
