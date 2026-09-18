Scriptname WayfarerRecovery Hidden

; Native validation consumes a single, expiring player request after the menu closes.
Bool Function ConsumePush(Actor target, Int ticket) Global Native

Bool Function Push(Actor target, Int ticket) Global
    If !ConsumePush(target, ticket)
        Return false
    EndIf
    ; Use the game's knockback/get-up sequence without altering AI ownership.
    Game.GetPlayer().PushActorAway(target, 1.0)
    Return true
EndFunction
