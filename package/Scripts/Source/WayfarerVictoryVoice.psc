Scriptname WayfarerVictoryVoice Hidden

; Compatibility only for saves made with the shelved victory experiment.
; The release DLL always returns false; no new holds or voice requests start.
bool Function HoldState(Actor speaker, int ticket, int status = 0) Global Native

Function Hold(Actor speaker, int ticket) Global
    if speaker && speaker != Game.GetPlayer()
        speaker.SetDontMove(false)
        speaker.EvaluatePackage()
    endif
EndFunction

Function Bark(Actor speaker, Topic line) Global
EndFunction
