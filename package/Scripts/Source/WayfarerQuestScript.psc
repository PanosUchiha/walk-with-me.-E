Scriptname WayfarerQuestScript extends Quest

Bool syncing = false
Actor[] lookingActors
Actor[] lookingTargets
; Zero means no request, including when these variables are added to an old save.
Int tetherPrioritySlot = 0
Int tetherPriorityTicket = 0

Event OnInit()
    Wake()
EndEvent

Function Wake()

    RegisterForSingleUpdate(0.1)
EndFunction

Function RequestTetherHandoff(Int slot, Int ticket)
    If slot >= 0 && slot < 10 && ticket != 0
        tetherPriorityTicket = ticket
        tetherPrioritySlot = slot + 1
        ; An idle sweep can service this immediately on this stack. A busy sweep
        ; keeps the request until its next safe slot boundary; never race cleanup.
        SyncAliases()
    EndIf
EndFunction

; Called ONLY by SyncAliases at slot boundaries, after all older slot work
; has returned. Never clear an alias from the independent request stack.
Bool Function ServiceTetherHandoff()
    If tetherPrioritySlot == 0
        Return true
    EndIf
    Int slot = tetherPrioritySlot - 1
    Int ticket = tetherPriorityTicket
    tetherPrioritySlot = 0
    ; Validate ticket, dialogue and slot ownership in one main-thread call.
    ReferenceAlias followerAlias = Wayfarer.GetTetherHandoffAlias(slot, ticket)
    If !followerAlias
        Return true
    EndIf
    Actor previous = followerAlias.GetActorReference()
    If previous
        If StopSyncForDialogue()
            Return false
        EndIf
        followerAlias.Clear()
        If StopSyncForDialogue()
            Return false
        EndIf
        previous.ClearKeepOffsetFromActor()
        If StopSyncForDialogue()
            Return false
        EndIf
        previous.EvaluatePackage()
    EndIf
    ; Native confirmation still requires BOTH an empty alias and released package.
    Wayfarer.ConfirmTetherHandoff(slot, ticket)
    Return true
EndFunction

Event OnUpdate()
    SyncAliases()
    RegisterForSingleUpdate(0.25)
EndEvent

Function SyncAliases()
    If syncing
        Return
    EndIf
    If Wayfarer.ShouldSuspendForDialogue()
        Return
    EndIf
    syncing = true
    Wayfarer.ReportAliasSync(true)
    ; Priority work precedes allocation/rest queries and the ordinary ten-slot sweep.
    If tetherPrioritySlot != 0
        If !ServiceTetherHandoff()
            Return
        EndIf
    EndIf
    If !lookingActors
        lookingActors = new Actor[10]
        lookingTargets = new Actor[10]
    EndIf
    Bool restingNow = Wayfarer.HasRestActivities()
    Int slot = 0
    While slot < 10
        If tetherPrioritySlot != 0
            If !ServiceTetherHandoff()
                Return
            EndIf
        EndIf
        If StopSyncForDialogue()
            Return
        EndIf
        Int followerID = slot
        Int markerID = slot + 5
        If slot >= 5
            followerID = slot + 5
            markerID = slot + 10
        EndIf
        ReferenceAlias followerAlias = GetAlias(followerID) as ReferenceAlias
        ReferenceAlias markerAlias = GetAlias(markerID) as ReferenceAlias

            ObjectReference strollTarget = Wayfarer.GetGatherMarker(slot)
            ReferenceAlias strollAlias = GetAlias(slot + 30) as ReferenceAlias
            If strollTarget && strollAlias && strollAlias.GetReference() != strollTarget
                If StopSyncForDialogue()
                    Return
                EndIf
                If Wayfarer.GetGatherMarker(slot) == strollTarget
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    strollAlias.ForceRefTo(strollTarget)
                EndIf
            EndIf
        If restingNow
            ReferenceAlias seatAlias = GetAlias(slot + 20) as ReferenceAlias
            ObjectReference seat = Wayfarer.GetRestSeat(slot)
            If seatAlias && seat && seatAlias.GetReference() != seat
                If StopSyncForDialogue()
                    Return
                EndIf
                seatAlias.ForceRefTo(seat)
            EndIf
        EndIf
        Int tetherTicket = 0
        If followerAlias && markerAlias
            tetherTicket = Wayfarer.GetTetherHandoffTicket(slot)
            Actor desired = Wayfarer.GetSlotFollower(slot)
            ObjectReference marker = Wayfarer.GetSlotMarker(slot)
            Actor previous = followerAlias.GetActorReference()
            If !marker
                desired = None
            EndIf
            If previous != desired
                If previous
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    followerAlias.Clear()
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    previous.ClearKeepOffsetFromActor()
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    previous.EvaluatePackage()
                EndIf
                If desired && marker
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    markerAlias.ForceRefTo(marker)

                    If StopSyncForDialogue()
                        Return
                    EndIf
                    desired.ClearKeepOffsetFromActor()
                    If Wayfarer.GetSlotFollower(slot) == desired
                        If StopSyncForDialogue()
                            Return
                        EndIf
                        followerAlias.ForceRefTo(desired)
                        If StopSyncForDialogue()
                            Return
                        EndIf
                        desired.EvaluatePackage()
                    EndIf
                EndIf
            ElseIf desired && markerAlias.GetReference() != marker
                If StopSyncForDialogue()
                    Return
                EndIf
                markerAlias.ForceRefTo(marker)
                If StopSyncForDialogue()
                    Return
                EndIf
                desired.EvaluatePackage()
            EndIf

            ; Acknowledge immediately after this slot's movement cleanup. Rest and
            ; social queries below must not add VM round trips to grip acquisition.
            If tetherTicket != 0
                Wayfarer.ConfirmTetherHandoff(slot, tetherTicket)
            EndIf
            If tetherPrioritySlot != 0
                If !ServiceTetherHandoff()
                    Return
                EndIf
            EndIf
            Actor current = followerAlias.GetActorReference()
            If current && Wayfarer.GetSlotFollower(slot) != current
                If StopSyncForDialogue()
                    Return
                EndIf
                followerAlias.Clear()
                If StopSyncForDialogue()
                    Return
                EndIf
                current.EvaluatePackage()
            EndIf
            If restingNow && current && Wayfarer.GetSlotFollower(slot) == current
                Idle resting = Wayfarer.ConsumeRestIdle(slot)
                If resting && Wayfarer.GetSlotFollower(slot) == current
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    Bool rested = current.PlayIdle(resting)
                    Wayfarer.ReportRestIdle(slot, current, resting, rested)
                EndIf
            EndIf
            Actor socialActor = None
            Actor target = None
            If restingNow && current
                socialActor = current
                target = Wayfarer.GetSocialTarget(slot)
            EndIf
            If lookingActors[slot] && (lookingActors[slot] != socialActor || lookingTargets[slot] != target || !target)
                If StopSyncForDialogue()
                    Return
                EndIf
                lookingActors[slot].ClearLookAt()
                lookingActors[slot] = None
                lookingTargets[slot] = None
            EndIf
            If socialActor && target && Wayfarer.GetSocialTarget(slot) == target
                If lookingActors[slot] != socialActor || lookingTargets[slot] != target
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    socialActor.SetLookAt(target)
                    lookingActors[slot] = socialActor
                    lookingTargets[slot] = target
                EndIf
                Idle gesture = Wayfarer.ConsumeSocialIdle(slot)
                If gesture && Wayfarer.GetSocialTarget(slot) == target
                    If StopSyncForDialogue()
                        Return
                    EndIf
                    Bool played = socialActor.PlayIdleWithTarget(gesture, target)
                    If !played && Wayfarer.GetSocialTarget(slot) == target
                        If StopSyncForDialogue()
                            Return
                        EndIf
                        played = socialActor.PlayIdle(gesture)
                    EndIf
                    Wayfarer.ReportSocialIdle(socialActor, played)
                EndIf
            EndIf
        EndIf
        slot += 1
    EndWhile
    If tetherPrioritySlot != 0
        If !ServiceTetherHandoff()
            Return
        EndIf
    EndIf
    Wayfarer.ReportAliasSync(false)
    syncing = false
EndFunction

Bool Function StopSyncForDialogue()
    If Wayfarer.ShouldSuspendForDialogue()
        Wayfarer.ReportAliasSync(false)
        syncing = false
        Return true
    EndIf
    Return false
EndFunction
