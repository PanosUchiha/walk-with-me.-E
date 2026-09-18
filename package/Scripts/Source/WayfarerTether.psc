Scriptname WayfarerTether Hidden

; TETHER physics/offset sequence adapted from KEI997 (MIT).
; Copyright (c) 2026 KEI997. See ThirdParty/TETHER-LICENSE.txt.
Bool Function Step(Int token, Int operation, Actor leader, Actor follower, Bool leftHand, Float px, Float py, Float pz, Float fx, Float fy, Float fz) Global
    Int status = Wayfarer.TetherStepStatus(token, operation, leader, follower)
    If status == 2
        ; The original actor/skeleton is gone; do not touch its replacement.
        Return true
    ElseIf status != 1
        Return false
    EndIf
    String playerHand = "NPC R Hand [RHnd]"
    String followerHand = "NPC L Hand [LHnd]"
    Float side = 40.0
    If leftHand
        playerHand = "NPC L Hand [LHnd]"
        followerHand = "NPC R Hand [RHnd]"
        side = -40.0
    EndIf
    If operation == 0
        leader.ForceAddRagdollToWorld()
    ElseIf operation == 1
        follower.ForceAddRagdollToWorld()
    ElseIf operation == 2
        Return Game.AddHavokBallAndSocketConstraint(leader, playerHand, follower, followerHand, px, py, pz, fx, fy, fz)
    ElseIf operation == 3
        Return Game.RemoveHavokConstraints(leader, playerHand, follower, followerHand)
    ElseIf operation == 4
        follower.ForceRemoveRagdollFromWorld()
    ElseIf operation == 5
        leader.ForceRemoveRagdollFromWorld()
    ElseIf operation == 6
        follower.KeepOffsetFromActor(leader, side, -10.0, 0.0, 0.0, 0.0, 0.0, 60.0, 10.0)
    ElseIf operation == 7
        follower.ClearKeepOffsetFromActor()
    Else
        Return false
    EndIf
    Return true
EndFunction
