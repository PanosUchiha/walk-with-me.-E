Scriptname WayfarerTether Hidden

; Physics sequence adapted from TETHER by KEI997, MIT.
; Copyright (c) 2026 KEI997. See ThirdParty/TETHER-LICENSE.txt.
; One checked operation per native callback: dispatch is not proof of completion.
Bool Function Step(Int token, Int operation, Actor leader, Actor follower, Bool leftHand) Global
    If !Wayfarer.TetherStepAllowed(token, operation, leader, follower)
        Return false
    EndIf
    String playerHand = "NPC R Hand [RHnd]"
    String followerHand = "NPC L Hand [LHnd]"
    If leftHand
        playerHand = "NPC L Hand [LHnd]"
        followerHand = "NPC R Hand [RHnd]"
    EndIf
    If operation == 0
        leader.ForceAddRagdollToWorld()
    ElseIf operation == 1
        follower.ForceAddRagdollToWorld()
    ElseIf operation == 2
        Return Game.AddHavokBallAndSocketConstraint(leader, playerHand, follower, followerHand, 0.0, 0.0, 2.0, 0.0, 0.0, 2.0)
    ElseIf operation == 3
        Return Game.RemoveHavokConstraints(leader, playerHand, follower, followerHand)
    ElseIf operation == 4
        follower.ForceRemoveRagdollFromWorld()
    ElseIf operation == 5
        leader.ForceRemoveRagdollFromWorld()
    Else
        Return false
    EndIf
    Return true
EndFunction
