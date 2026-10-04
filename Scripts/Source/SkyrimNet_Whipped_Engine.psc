Scriptname SkyrimNet_Whipped_Engine Hidden

; Natives from SkyrimNet_Whipped.dll (SKSE_Source/src/Papyrus_Engine.cpp).
; DamageHealing_Engine: per-actor damage and healing for six body parts
; (0 face, 1 left arm, 2 right arm, 3 back, 4 left leg, 5 right leg).
; Wounds hold health down (90% of max health at full damage); healing above that converts back
; into wound points in SyncHealth.

; Damages a random part and takes the matching health (never below 10% of max health). Returns part * 4 + hit level (0 undamaged, 1 light, 2 medium, 3 severe;
; the part's state before the hit), or -1 for None/dead actors.
Int Function Hit(Actor akActor) global native

; True if any hit part still has damage (False: untracked, or every hit part is healing).
Bool Function HasOpenWounds(Actor akActor) global native

; Applies time to every tracked actor; returns those with parts that healed to a new stage
; since the last TakeHealed.
Actor[] Function TakeHealedActors() global native

; part * 8 + stage (0 light, 1 medium, 2 severe, 3 recovering, 4 mostly healed, 5 healed) for each
; part that healed to a new stage (game time, or health converted by SyncHealth); clears them.
Int[] Function TakeHealed(Actor akActor) global native

; Converts healing on loaded tracked actors into wound points (all of it while a healing source is
; active, natural regen at the regen rate), one point at a time on a random part. Returns the queued events as
; kind * 10000 + source * 1000 + percent:
;   kind: 0 heal start, 1 pain gone (damage reached 0), 2 fully healed (actor forgotten), 3 heal stopped
;   source: 0 none (regen / game time), 1 spell, 2 staff, 3 potion, 4 other magic
; SyncActors / SyncCasters are parallel to the last result (caster may be None).
Int[] Function SyncHealth() global native
Actor[] Function SyncActors() global native
Actor[] Function SyncCasters() global native

; Tracked actors plus events not yet taken by SyncHealth.
Int Function TrackedCount() global native

; Narration noun for a part ("face", "left arm", ...); "" when the .dll is missing.
String Function PartName(Int part) global native

; {"parts":[{"part":"left arm","region":"arm","stage":"medium","text":8,"damage":5,"healing":0},...]}
String Function GetStateJson(Actor akActor) global native

; Game hours for one point of damage to drain (or one point of healing to accumulate).
Function SetHoursPerPoint(Float hours) global native

; Percent of natural regeneration that heals wounds; the rest of that regen is taken back.
Function SetRegenRate(Float percent) global native

; "Show whipping overlay": PrismaUI bars per body part for whipped / healing actors.
Function SetOverlayEnabled(Bool enabled) global native

Function Forget(Actor akActor) global native
