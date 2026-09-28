Scriptname WayfarerQuestScript extends Quest

Bool syncing = false
Actor[] lookingActors
Actor[] lookingTargets
Actor[] releasingActors
Bool[] releasePending
ReferenceAlias[] followerAliases
ReferenceAlias[] markerAliases
ReferenceAlias[] gatherAliases
ReferenceAlias[] seatAliases
; Zero means no request, including when these variables are added to an old save.
Int tetherPrioritySlot = 0
Int tetherPriorityTicket = 0

Event OnInit()
    Wake()
EndEvent

Function Wake()

    ; Retire timers restored from older saves. Native requests now drive the bridge.
    UnregisterForUpdate()
EndFunction

Function RequestTetherHandoff(Int slot, Int ticket)
    If slot >= 0 && slot < 10 && ticket != 0
        tetherPriorityTicket = ticket
        tetherPrioritySlot = slot + 1
        ; The active sweep services this at a safe boundary. Otherwise the native
        ; dispatcher observes the ticket and starts one sweep for this slot.
    EndIf
EndFunction

; Called only by the serialized sweep at slot boundaries, after older slot work
; has returned. Never clear an alias from the independent request stack.
Bool Function ServiceTetherHandoff()
    ; Keep the old argument count for call instructions already stored in saves.
    Return ServiceRequestedHandoff(0, 0)
EndFunction

Bool Function ServiceRequestedHandoff(Int request, Int session)
    EnsureReleaseStorage()
    If tetherPrioritySlot == 0
        Return true
    EndIf
    Int slot = tetherPrioritySlot - 1
    Int ticket = tetherPriorityTicket
    tetherPrioritySlot = 0
    If !FinishAliasRelease(slot, request, session)
        Return false
    EndIf
    ; Validate ticket, dialogue and slot ownership in one main-thread call.
    ReferenceAlias followerAlias = Wayfarer.GetTetherHandoffAlias(slot, ticket)
    If !followerAlias
        Return true
    EndIf
    Actor previous = followerAlias.GetActorReference()
    If previous
        If StopRequestedSync(request, session)
            Return false
        EndIf
        releasingActors[slot] = previous
        releasePending[slot] = true
        Wayfarer.ReportAliasCleanup(slot, true, request, session)
        If StopRequestedSync(request, session)
            Return false
        EndIf
        followerAlias.Clear()
        If !FinishAliasRelease(slot, request, session)
            Return false
        EndIf
    EndIf
    ; Native confirmation still requires BOTH an empty alias and released package.
    Wayfarer.ConfirmTetherHandoff(slot, ticket)
    Return true
EndFunction

Event OnUpdate()
    ; An old saved OnUpdate may re-arm once when its saved body finishes.
    UnregisterForUpdate()
EndEvent

Function SyncAliases()
    ; Compatibility entry point for older callers; native work detection owns dispatch.
    UnregisterForUpdate()
EndFunction

Bool Function SyncRequested(Int slots, Int request, Int session)
    If syncing
        Return false
    EndIf
    ; Acquire before any native call can yield to another stack.
    syncing = true
    If StopRequestedSync(request, session)
        Return false
    EndIf
    UnregisterForUpdate()
    Wayfarer.ReportAliasSync(true)
    EnsureReleaseStorage()
    ; Priority work precedes allocation/rest queries and the ordinary ten-slot sweep.
    If tetherPrioritySlot != 0
        If !ServiceRequestedHandoff(request, session)
            Return false
        EndIf
    EndIf
    If !lookingActors
        lookingActors = new Actor[10]
        lookingTargets = new Actor[10]
    EndIf
    If !followerAliases
        followerAliases = new ReferenceAlias[10]
        markerAliases = new ReferenceAlias[10]
        gatherAliases = new ReferenceAlias[10]
        seatAliases = new ReferenceAlias[10]
    EndIf
    Bool restingNow = Wayfarer.HasRestActivities()
    Int slot = 0
    While slot < 10
        If slots % 2 != 0
            If tetherPrioritySlot != 0
                If !ServiceRequestedHandoff(request, session)
                    Return false
                EndIf
            EndIf
            If StopRequestedSync(request, session)
                Return false
            EndIf
            If !FinishAliasRelease(slot, request, session)
                Return false
            EndIf
            Int followerID = slot
            Int markerID = slot + 5
            If slot >= 5
                followerID = slot + 5
                markerID = slot + 10
            EndIf
            If !followerAliases[slot]
                followerAliases[slot] = GetAlias(followerID) as ReferenceAlias
            EndIf
            If !markerAliases[slot]
                markerAliases[slot] = GetAlias(markerID) as ReferenceAlias
            EndIf
            ReferenceAlias followerAlias = followerAliases[slot]
            ReferenceAlias markerAlias = markerAliases[slot]

                ObjectReference strollTarget = Wayfarer.GetGatherMarker(slot)
                If strollTarget && !gatherAliases[slot]
                    gatherAliases[slot] = GetAlias(slot + 30) as ReferenceAlias
                EndIf
                ReferenceAlias strollAlias = gatherAliases[slot]
                If strollTarget && strollAlias && strollAlias.GetReference() != strollTarget
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    If Wayfarer.GetGatherMarker(slot) == strollTarget
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        strollAlias.ForceRefTo(strollTarget)
                    EndIf
                EndIf
            If restingNow
                ObjectReference seat = Wayfarer.GetRestSeat(slot)
                If seat && !seatAliases[slot]
                    seatAliases[slot] = GetAlias(slot + 20) as ReferenceAlias
                EndIf
                ReferenceAlias seatAlias = seatAliases[slot]
                If seatAlias && seat && seatAlias.GetReference() != seat
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    seatAlias.ForceRefTo(seat)
                EndIf
            EndIf
            Int tetherTicket = 0
            If !followerAlias || !markerAlias
                Wayfarer.ReportAliasSync(false)
                syncing = false
                Return false
            EndIf
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
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        releasingActors[slot] = previous
                        releasePending[slot] = true
                        Wayfarer.ReportAliasCleanup(slot, true, request, session)
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        followerAlias.Clear()
                        If !FinishAliasRelease(slot, request, session)
                            Return false
                        EndIf
                    EndIf
                    If desired && marker
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        markerAlias.ForceRefTo(marker)

                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        desired.ClearKeepOffsetFromActor()
                        If Wayfarer.GetSlotFollower(slot) == desired
                            If StopRequestedSync(request, session)
                                Return false
                            EndIf
                            followerAlias.ForceRefTo(desired)
                            If StopRequestedSync(request, session)
                                Return false
                            EndIf
                            desired.EvaluatePackage()
                        EndIf
                    EndIf
                ElseIf desired && markerAlias.GetReference() != marker
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    markerAlias.ForceRefTo(marker)
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    desired.EvaluatePackage()
                EndIf

                ; Acknowledge immediately after this slot's movement cleanup. Rest and
                ; social queries below must not add VM round trips to grip acquisition.
                If tetherTicket != 0
                    Wayfarer.ConfirmTetherHandoff(slot, tetherTicket)
                EndIf
                If tetherPrioritySlot != 0
                    If !ServiceRequestedHandoff(request, session)
                        Return false
                    EndIf
                EndIf
                Actor current = followerAlias.GetActorReference()
                If current && Wayfarer.GetSlotFollower(slot) != current
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    releasingActors[slot] = current
                    releasePending[slot] = true
                    Wayfarer.ReportAliasCleanup(slot, true, request, session)
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    followerAlias.Clear()
                    If !FinishAliasRelease(slot, request, session)
                        Return false
                    EndIf
                EndIf
                If restingNow && current && Wayfarer.GetSlotFollower(slot) == current
                    Idle resting = Wayfarer.ConsumeRestIdle(slot)
                    If resting && Wayfarer.GetSlotFollower(slot) == current
                        If StopRequestedSync(request, session)
                            Return false
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
                    If StopRequestedSync(request, session)
                        Return false
                    EndIf
                    lookingActors[slot].ClearLookAt()
                    lookingActors[slot] = None
                    lookingTargets[slot] = None
                EndIf
                If socialActor && target && Wayfarer.GetSocialTarget(slot) == target
                    If lookingActors[slot] != socialActor || lookingTargets[slot] != target
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        socialActor.SetLookAt(target)
                        lookingActors[slot] = socialActor
                        lookingTargets[slot] = target
                    EndIf
                    Idle gesture = Wayfarer.ConsumeSocialIdle(slot)
                    If gesture && Wayfarer.GetSocialTarget(slot) == target
                        If StopRequestedSync(request, session)
                            Return false
                        EndIf
                        Bool played = socialActor.PlayIdleWithTarget(gesture, target)
                        If !played && Wayfarer.GetSocialTarget(slot) == target
                            If StopRequestedSync(request, session)
                                Return false
                            EndIf
                            played = socialActor.PlayIdle(gesture)
                        EndIf
                        Wayfarer.ReportSocialIdle(socialActor, played)
                    EndIf
                EndIf
            EndIf
            Wayfarer.ReportAliasLook(slot, lookingActors[slot], lookingTargets[slot], request, session)
        EndIf
        slots = slots / 2
        slot += 1
    EndWhile
    If tetherPrioritySlot != 0
        If !ServiceRequestedHandoff(request, session)
            Return false
        EndIf
    EndIf
    Wayfarer.ReportAliasSync(false)
    syncing = false
    Return true
EndFunction

Bool Function StopSyncForDialogue()
    ; Keep the legacy signature while old saved sweeps drain.
    Return StopRequestedSync(0, 0)
EndFunction

Bool Function StopRequestedSync(Int request, Int session)
    Bool stop = false
    If request != 0
        stop = !Wayfarer.AliasSyncAllowed(request, session)
    Else
        ; Compatibility for a legacy frame still finishing its old saved body.
        stop = Wayfarer.ShouldSuspendForDialogue()
    EndIf
    If stop
        Wayfarer.ReportAliasSync(false)
        syncing = false
        Return true
    EndIf
    Return false
EndFunction

; Keep interrupted cleanup across dialogue and save/load, including a cleared alias.
Function EnsureReleaseStorage()
    If !releasingActors
        releasingActors = new Actor[10]
        releasePending = new Bool[10]
    EndIf
EndFunction

Bool Function FinishAliasRelease(Int slot, Int request, Int session)
    If !releasePending[slot]
        Return true
    EndIf
    Actor previous = releasingActors[slot]
    If previous
        If StopRequestedSync(request, session)
            Return false
        EndIf
        previous.ClearKeepOffsetFromActor()
        If StopRequestedSync(request, session)
            Return false
        EndIf
        previous.EvaluatePackage()
    EndIf
    releasingActors[slot] = None
    releasePending[slot] = false
    Wayfarer.ReportAliasCleanup(slot, false, request, session)
    Return true
EndFunction
