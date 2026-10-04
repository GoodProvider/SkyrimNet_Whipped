Scriptname SkyrimNet_Whipped_Main extends Quest

; Whip marks:
;   The visible welt is an engine impact decal (WEAP -> IPDS -> IPCT -> TXST), copied from
;   PAH Diary Of Mine's padded whip. The engine owns decals (they vanish on cell change or
;   reload), so the wounds the LLM is told about are tracked separately by the C++
;   DamageHealing_Engine (SkyrimNet_Whipped_Engine natives): per body part damage, then healing.

Weapon Property PaddedWhip Auto
ImpactDataSet Property ImpactSet Auto
GlobalVariable Property require_whip_global Auto
Spell Property HealingHands Auto

String Property PLUGIN_CONFIG = "Plugin_SkyrimNet_Whipped" AutoReadOnly
String Property KEY_LAST_NARRATION = "skyrimnet_whipped.last_narration" AutoReadOnly
String Property KEY_IN_ACTION = "skyrimnet_whipped.in_action" AutoReadOnly
; Lashes since the victim's last narration: part bitmask, worst hit level, count
String Property KEY_PENDING_PARTS = "skyrimnet_whipped.pending_parts" AutoReadOnly
String Property KEY_PENDING_LEVEL = "skyrimnet_whipped.pending_level" AutoReadOnly
String Property KEY_PENDING_COUNT = "skyrimnet_whipped.pending_count" AutoReadOnly
; Heals since the victim's last heal narration: IntList of part * 8 + stage, count; heal_narrated is
; set once a heal was narrated since the last lash (unset = the next heal is fresh)
String Property KEY_HEAL_PENDING = "skyrimnet_whipped.heal_pending" AutoReadOnly
String Property KEY_HEAL_COUNT = "skyrimnet_whipped.heal_count" AutoReadOnly
String Property KEY_HEAL_NARRATED = "skyrimnet_whipped.heal_narrated" AutoReadOnly

Float Property UNITS_PER_METER = 70.0 AutoReadOnly

; Plugin config (dashboard), refreshed by ApplyPluginConfig
Bool Property require_whip = True Auto Hidden
Bool Property give_whip_to_player = True Auto Hidden
Int Property max_strikes = 10 Auto Hidden
Float Property hours_per_point = 1.0 Auto Hidden
Bool Property give_healing_hands = True Auto Hidden
Float Property regen_rate = 10.0 Auto Hidden
Bool Property narration_enabled = True Auto Hidden
Float Property narration_cooldown = 8.0 Auto Hidden
Float Property narration_max_distance = 15.0 Auto Hidden
Float Property event_ttl = 30.0 Auto Hidden

Bool Property dll_ok = False Auto Hidden

Function Trace(String func, String msg, Bool notification=False) global
    Debug.Trace("[SkyrimNet_Whipped_Main] " + func + ": " + msg)
    if notification
        Debug.Notification(msg)
    endif
EndFunction

; Every DirectNarration goes through here so the exact text sent is in the Papyrus log.
Function DirectNarrate(String msg, Actor speaker, Actor target) global
    Trace("DirectNarration", NameOf(speaker) + " -> " + NameOf(target) + ": " + msg)
    SkyrimNetApi.DirectNarration(msg, speaker, target)
EndFunction

SkyrimNet_Whipped_Main Function Get() global
    return Game.GetFormFromFile(0x800, "SkyrimNet_Whipped.esp") as SkyrimNet_Whipped_Main
EndFunction

;----------------------------------------------------------------------------------------------------
; Setup
;----------------------------------------------------------------------------------------------------
Function Setup()
    Trace("Setup", "")
    if !Setup_CheckLinks()
        return
    endif

    UnRegisterForModEvent("SkyrimNet_OnPluginConfigSaved")
    RegisterForModEvent("SkyrimNet_OnPluginConfigSaved", "OnPluginConfigSaved")
    ApplyPluginConfig()

    Actor player = Game.GetPlayer()
    int have = player.GetItemCount(PaddedWhip)
    if !give_whip_to_player
        Trace("Setup", "give_whip_to_player is off; player has " + have + " whip(s)")
    elseif have > 0
        Trace("Setup", "player already has " + have + " whip(s); not giving another")
    else
        player.AddItem(PaddedWhip, 1, true)
        Trace("Setup", "gave " + PaddedWhip.GetName() + " to player (now " + player.GetItemCount(PaddedWhip) + ")")
    endif
    GiveHealingHands()

    dll_ok = SkyrimNet_Whipped_Engine.PartName(0) != ""
    if !dll_ok
        Trace("Setup", "ERROR: SkyrimNet_Whipped.dll is missing; whip wounds will not be tracked.", true)
    else
        StartSync() ; actors loaded from the co-save
    endif
    SkyrimNetApi.RegisterDecorator("skyrimnet_whipped_welts", "SkyrimNet_Whipped_Main", "Welts_Decorator")

    SkyrimNet_Whipped_Actions actions = (self as Quest) as SkyrimNet_Whipped_Actions
    if actions != None
        actions.Setup()
    else
        Trace("Setup", "ERROR: Failed to get SkyrimNet_Whipped_Actions.", true)
    endif
EndFunction

Bool Function Setup_CheckLinks()
    Bool links_ok = true
    if PaddedWhip == None
        Trace("Setup_CheckLinks", "ERROR: PaddedWhip is None", true)
        links_ok = false
    endif
    if ImpactSet == None
        Trace("Setup_CheckLinks", "ERROR: ImpactSet is None", true)
        links_ok = false
    endif
    return links_ok
EndFunction

Event OnPluginConfigSaved(string eventName, string strArg, float numArg, Form sender)
    ApplyPluginConfig()
    GiveHealingHands()
EndEvent

; Turning the setting off never removes the spell (the player may have learned it).
Function GiveHealingHands()
    if !give_healing_hands || HealingHands == None
        return
    endif
    Actor player = Game.GetPlayer()
    if !player.HasSpell(HealingHands)
        player.AddSpell(HealingHands, false)
        Trace("GiveHealingHands", "gave " + HealingHands.GetName() + " to player")
    endif
EndFunction

Function ApplyPluginConfig()
    require_whip = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.action.require_whip", true)
    give_whip_to_player = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.action.give_to_player", true)
    max_strikes = SkyrimNetApi.GetConfigInt(PLUGIN_CONFIG, "whip.action.max_strikes", 10)
    hours_per_point = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.hours_per_point", 1.0)
    give_healing_hands = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.healing.give_healing_hands", true)
    regen_rate = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.healing.regen_rate", 10.0)
    narration_enabled = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.narration.enabled", true)
    narration_cooldown = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.cooldown", 8.0)
    narration_max_distance = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.max_distance", 15.0)
    event_ttl = SkyrimNetApi.GetConfigFloat(PLUGIN_CONFIG, "whip.narration.event_ttl", 30.0)
    Bool show_overlay = SkyrimNetApi.GetConfigBool(PLUGIN_CONFIG, "whip.overlay.enabled", true)
    if max_strikes < 1
        max_strikes = 1
    endif
    SkyrimNet_Whipped_Engine.SetHoursPerPoint(hours_per_point)
    SkyrimNet_Whipped_Engine.SetRegenRate(regen_rate)
    SkyrimNet_Whipped_Engine.SetOverlayEnabled(show_overlay)
    RegisterForSingleUpdateGameTime(hours_per_point) ; healing poll follows the heal rate
    if require_whip_global
        require_whip_global.SetValue(require_whip as Float)
    endif
    Trace("ApplyPluginConfig", "require_whip:" + require_whip + " give_whip_to_player:" + give_whip_to_player + " max_strikes:" + max_strikes + " hours_per_point:" + hours_per_point \
        + " give_healing_hands:" + give_healing_hands + " regen_rate:" + regen_rate \
        + " narration:" + narration_enabled + " cooldown:" + narration_cooldown + " max_distance:" + narration_max_distance + " event_ttl:" + event_ttl + " overlay:" + show_overlay)
EndFunction

;----------------------------------------------------------------------------------------------------
; Lashes
;----------------------------------------------------------------------------------------------------


; Scripted strike (LLM action): same decal and sound as a real weapon hit, then OnLash.
; Returns OnLash's hit code.
Int Function Lash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None
        return -1
    endif
    String node = "NPC Spine2 [Spn2]"
    int roll = Utility.RandomInt(0, 2)
    if roll == 1
        node = "NPC Spine1 [Spn1]"
    elseif roll == 2
        node = "NPC Pelvis [Pelv]"
    endif
    ; Pick from the attacker's side toward the victim so the decal lands on the facing skin.
    float dx = 0.0
    float dy = 0.0
    if attacker != None
        dx = victim.GetPositionX() - attacker.GetPositionX()
        dy = victim.GetPositionY() - attacker.GetPositionY()
        float len = Math.sqrt(dx * dx + dy * dy)
        if len > 0.0
            dx = dx / len
            dy = dy / len
        endif
    endif
    ; The flesh impact (IPCT) carries the hit sound.
    Trace("Lash", "[dbg] " + NameOf(attacker) + " -> " + victim.GetDisplayName() + " node:" + node) ; DEBUG-WHIP
    victim.PlayImpactEffect(ImpactSet, node, dx, dy, 0.0, 128.0)
    return OnLash(attacker, victim, narrate)
EndFunction

; Every strike ends here: the engine damages a random body part, then narrate.
; Returns part * 4 + hit level (see SkyrimNet_Whipped_Engine.Hit), or -1.
Int Function OnLash(Actor attacker, Actor victim, Bool narrate=True)
    if victim == None || victim.IsDead()
        return -1
    endif
    Bool fresh = !SkyrimNet_Whipped_Engine.HasOpenWounds(victim)
    float dbgHealth = victim.GetActorValue("Health") ; DEBUG-WHIP
    int code = SkyrimNet_Whipped_Engine.Hit(victim)
    if code < 0
        return code
    endif
    Trace("OnLash", "[dbg] health " + dbgHealth + " -> " + victim.GetActorValue("Health") + " / " + victim.GetBaseActorValue("Health") \
        + " state:" + SkyrimNet_Whipped_Engine.GetStateJson(victim)) ; DEBUG-WHIP
    Trace("OnLash", NameOf(attacker) + " -> " + victim.GetDisplayName() + " part:" + SkyrimNet_Whipped_Engine.PartName(code / 4) \
        + " level:" + (code % 4) + " fresh:" + fresh)
    if narrate
        NarrateLash(attacker, victim, code, fresh)
    endif
    ClearHealBatch(victim) ; pending heals are stale; the next heal is fresh
    StartSync()
    return code
EndFunction

; Lashes are batched per victim (KEY_PENDING_*):
;   - fresh (no open wounds before this hit): always narrated; an event when narration is off or too far
;   - later hits: when the speech queue is empty and the cooldown has passed, one narration of every
;     part hit since the last narration at the worst level; otherwise a short-lived event per hit
Function NarrateLash(Actor attacker, Actor victim, int code, Bool fresh)
    if fresh
        ClearBatch(victim)
    endif
    int part_bit = Math.LeftShift(1, code / 4)
    int level = code % 4
    int mask = Math.LogicalOr(StorageUtil.GetIntValue(victim, KEY_PENDING_PARTS, 0), part_bit)
    int worst = StorageUtil.GetIntValue(victim, KEY_PENDING_LEVEL, -1)
    if level > worst
        worst = level
    endif
    int count = StorageUtil.GetIntValue(victim, KEY_PENDING_COUNT, 0) + 1
    StorageUtil.SetIntValue(victim, KEY_PENDING_PARTS, mask)
    StorageUtil.SetIntValue(victim, KEY_PENDING_LEVEL, worst)
    StorageUtil.SetIntValue(victim, KEY_PENDING_COUNT, count)

    Bool narrate = narration_enabled && !TooFar(victim)
    if fresh || (narrate && NarrationAllowed(victim))
        String msg = LashBatchText(attacker, victim, mask, worst, count)
        ClearBatch(victim)
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        if narrate
            Trace("NarrateLash", "narration fresh:" + fresh + " lashes:" + count)
            DirectNarrate(msg, attacker, victim)
        else
            Trace("NarrateLash", "event fresh:" + fresh)
            SkyrimNetApi.RegisterEvent("whip_lash", msg, attacker, victim)
        endif
        return
    endif
    Trace("NarrateLash", "short-lived event, pending lashes:" + count)
    SkyrimNetApi.RegisterShortLivedEvent("whip_lash_" + victim.GetFormID() + "_" + count, "whip_lash", \
        LashBatchText(attacker, victim, part_bit, level, 1), "", (event_ttl * 1000.0) as int, attacker, victim)
EndFunction

Function ClearBatch(Actor victim)
    StorageUtil.UnsetIntValue(victim, KEY_PENDING_PARTS)
    StorageUtil.UnsetIntValue(victim, KEY_PENDING_LEVEL)
    StorageUtil.UnsetIntValue(victim, KEY_PENDING_COUNT)
EndFunction

Bool Function TooFar(Actor akActor)
    return !akActor.Is3DLoaded() || Game.GetPlayer().GetDistance(akActor) > narration_max_distance * UNITS_PER_METER
EndFunction

; Speech queue empty and the victim's narration cooldown has passed.
Bool Function NarrationAllowed(Actor victim)
    if SkyrimNetApi.GetSpeechQueueSize() > 0
        return false
    endif
    float now = Utility.GetCurrentRealTime()
    float last = StorageUtil.GetFloatValue(victim, KEY_LAST_NARRATION, -1000.0)
    ; real time restarts each session, so a "last" in the future is stale
    return !(last <= now && (now - last) < narration_cooldown)
EndFunction

; "face", "back and left arm", "face, back and left arm" for a part bitmask ("body" if empty).
String Function PartsText(int mask)
    String text = ""
    int remaining = mask
    int i = 0
    while i < 6 && remaining != 0
        int bit = Math.LeftShift(1, i)
        if Math.LogicalAnd(remaining, bit) != 0
            remaining -= bit
            String part = SkyrimNet_Whipped_Engine.PartName(i)
            if text == ""
                text = part
            elseif remaining == 0
                text += " and " + part
            else
                text += ", " + part
            endif
        endif
        i += 1
    endwhile
    if text == ""
        text = "body"
    endif
    return text
EndFunction

; One lash, code = part * 4 + level. Used by the Whip_Target summary.
String Function LashText(Actor attacker, Actor victim, int code)
    return LashBatchText(attacker, victim, Math.LeftShift(1, code / 4), code % 4, 1)
EndFunction

; "<Name>'s <parts> is/are hit [N times] by <attacker>'s whip, ..." with the description for the
; worst part state before a hit (level: 0 undamaged, 1 light, 2 medium, 3 severe).
String Function LashBatchText(Actor attacker, Actor victim, int mask, int level, int count)
    String name = victim.GetDisplayName()
    String he = "he"
    String him = "him"
    String his = "his"
    if victim.GetLeveledActorBase().GetSex() == 1
        he = "she"
        him = "her"
        his = "her"
    endif
    String by = " with the whip"
    if attacker != None && attacker != victim
        by = " by " + attacker.GetDisplayName() + "'s whip"
    endif
    String verb = " is hit"
    if Math.LogicalAnd(mask, mask - 1) != 0
        verb = " are hit"
    endif
    if count == 2
        verb += " twice"
    elseif count > 4
        verb += " again and again"
    elseif count > 1
        verb += " several times"
    endif
    String hit = name + "'s " + PartsText(mask) + verb + by

    if level == 0
        return hit + ", a sudden, sharp slap of raw shock. The bite is clean and hot, an electric burst that rattles " \
            + his + " nerves and makes " + him + " flinch, leaving a pulsing throb behind as " + his + " body scrambles to react."
    elseif level == 1
        return hit + " across skin that is already tender. The dull ache spikes into a searing, white-hot flash as the lash digs into swollen, inflamed flesh, driving the breath from " \
            + his + " lungs and turning a sore spot into radiating fire."
    elseif level == 2
        return hit + ", and the blow tears open knitting skin and crushes half-healed muscle. The steady throb becomes a blinding, nauseating jolt that drops straight into " \
            + his + " gut, breaking " + his + " composure and forcing an involuntary gasp or moan from " + him + "."
    endif
    return hit + ", and it is too much. The strike feels less like pain than demolition, a whiteout of primal agony that throws " \
        + him + " into shock: the blood drains from " + his + " face, cold sweat breaks out, " + his + " vision tunnels, and " \
        + he + " loses all control of " + his + " body."
EndFunction

;----------------------------------------------------------------------------------------------------
; Healing: parts that healed to a new stage (game time, or health converted by SyncHealth) are taken
; every hours_per_point game hours and on every sync tick, and narrated like lashes (NarrateHealing).
;----------------------------------------------------------------------------------------------------
Event OnUpdateGameTime()
    if !dll_ok
        return
    endif
    NarrateHealedActors()
    if SkyrimNet_Whipped_Engine.TrackedCount() > 0
        StartSync() ; flush fully-healed events queued while applying time
    endif
    RegisterForSingleUpdateGameTime(hours_per_point)
EndEvent

Function NarrateHealedActors()
    Actor[] healed = SkyrimNet_Whipped_Engine.TakeHealedActors()
    int i = 0
    while i < healed.Length
        NarrateHealing(healed[i])
        i += 1
    endwhile
EndFunction

; Heals are batched per victim (KEY_HEAL_*), mirroring NarrateLash:
;   - fresh (no heal narrated since the last lash): always narrated; an event when narration is off or too far
;   - later heals: when the speech queue is empty and the cooldown has passed, one narration of every
;     part healed since the last narration at its latest stage; otherwise a short-lived event per take
Function NarrateHealing(Actor victim)
    if victim == None
        return
    endif
    int[] codes = SkyrimNet_Whipped_Engine.TakeHealed(victim)
    Trace("NarrateHealing", "[dbg] " + victim.GetDisplayName() + " codes:" + codes + " state:" + SkyrimNet_Whipped_Engine.GetStateJson(victim)) ; DEBUG-WHIP
    if codes.Length == 0 || victim.IsDead()
        return
    endif
    Bool fresh = StorageUtil.GetIntValue(victim, KEY_HEAL_NARRATED, 0) == 0
    int i = 0
    while i < codes.Length
        StorageUtil.IntListAdd(victim, KEY_HEAL_PENDING, codes[i])
        i += 1
    endwhile
    int count = StorageUtil.GetIntValue(victim, KEY_HEAL_COUNT, 0) + 1
    StorageUtil.SetIntValue(victim, KEY_HEAL_COUNT, count)

    Bool narrate = narration_enabled && !TooFar(victim)
    if fresh || (narrate && NarrationAllowed(victim))
        String msg = HealText(victim, LatestPerPart(StorageUtil.IntListToArray(victim, KEY_HEAL_PENDING)))
        StorageUtil.IntListClear(victim, KEY_HEAL_PENDING)
        StorageUtil.UnsetIntValue(victim, KEY_HEAL_COUNT)
        if msg == ""
            return
        endif
        StorageUtil.SetIntValue(victim, KEY_HEAL_NARRATED, 1)
        StorageUtil.SetFloatValue(victim, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        if narrate
            Trace("NarrateHealing", victim.GetDisplayName() + " narration fresh:" + fresh + " takes:" + count)
            DirectNarrate(msg, victim, None)
        else
            Trace("NarrateHealing", victim.GetDisplayName() + " event fresh:" + fresh + ": " + msg)
            SkyrimNetApi.RegisterEvent("whip_heal", msg, victim, None)
        endif
        return
    endif
    String take = HealText(victim, codes)
    if take == ""
        return
    endif
    Trace("NarrateHealing", victim.GetDisplayName() + " short-lived event, pending takes:" + count + ": " + take)
    SkyrimNetApi.RegisterShortLivedEvent("whip_heal_" + victim.GetFormID() + "_" + count, "whip_heal", \
        take, "", (event_ttl * 1000.0) as int, victim, None)
EndFunction

Function ClearHealBatch(Actor victim)
    StorageUtil.IntListClear(victim, KEY_HEAL_PENDING)
    StorageUtil.UnsetIntValue(victim, KEY_HEAL_COUNT)
    StorageUtil.UnsetIntValue(victim, KEY_HEAL_NARRATED)
EndFunction

; Pending heal codes collapsed to the latest stage per part (part order; -1 = none).
int[] Function LatestPerPart(int[] codes)
    int[] latest = new int[6]
    int i = 0
    while i < 6
        latest[i] = -1
        i += 1
    endwhile
    i = 0
    while i < codes.Length
        latest[codes[i] / 8] = codes[i]
        i += 1
    endwhile
    return latest
EndFunction

; One sentence per part, codes = part * 8 + new stage (see SkyrimNet_Whipped_Engine.TakeHealed); -1 skipped.
; Wording lives in prompts/helpers/healing.prompt (kind 4, stages); the fallback names each part.
String Function HealText(Actor victim, int[] codes)
    String name = victim.GetDisplayName()
    String parts = ""
    String fallback = ""
    int i = 0
    while i < codes.Length
        int stage = codes[i] % 8
        if codes[i] >= 0 && (stage == 0 || stage == 1 || stage == 3 || stage == 4 || stage == 5)
            String part = SkyrimNet_Whipped_Engine.PartName(codes[i] / 8)
            if parts != ""
                parts += ","
                fallback += " "
            endif
            parts += "{\"part\":" + JsonString(part) + ",\"stage\":" + stage + "}"
            if stage == 5
                fallback += "The whip wound on " + name + "'s " + part + " has fully healed."
            else
                fallback += "The whip wound on " + name + "'s " + part + " is healing."
            endif
        endif
        i += 1
    endwhile
    if parts == ""
        return ""
    endif
    int female = 0
    if victim.GetLeveledActorBase().GetSex() == 1
        female = 1
    endif
    String json = "{\"kind\":" + HEAL_STAGES + ",\"name\":" + JsonString(name) + ",\"female\":" + female \
        + ",\"caster\":\"\",\"caster_is_target\":0,\"source\":" + SOURCE_NONE + ",\"percent\":0,\"parts\":[" + parts + "]}"
    return RenderHealPrompt(json, fallback)
EndFunction

;----------------------------------------------------------------------------------------------------
; Health sync: wounds hold health down, healing above that converts back into wound points
; (SkyrimNet_Whipped_Engine.SyncHealth). Runs every 0.2 s while any actor is tracked.
;----------------------------------------------------------------------------------------------------
Int Property HEAL_START = 0 AutoReadOnly
Int Property HEAL_PAIN_GONE = 1 AutoReadOnly
Int Property HEAL_FULL = 2 AutoReadOnly
Int Property HEAL_STOPPED = 3 AutoReadOnly
Int Property HEAL_STAGES = 4 AutoReadOnly ; healing.prompt only: per-part stage changes
Int Property SOURCE_NONE = 0 AutoReadOnly
Int Property SOURCE_SPELL = 1 AutoReadOnly
Int Property SOURCE_STAFF = 2 AutoReadOnly
Int Property SOURCE_POTION = 3 AutoReadOnly

Float Property SYNC_INTERVAL = 0.2 AutoReadOnly

; Replaces any pending registration, so calling it again never doubles the loop.
Function StartSync()
    if dll_ok
        RegisterForSingleUpdate(SYNC_INTERVAL)
    endif
EndFunction

Event OnUpdate()
    int[] codes = SkyrimNet_Whipped_Engine.SyncHealth()
    if codes.Length > 0
        Actor[] targets = SkyrimNet_Whipped_Engine.SyncActors()
        Actor[] casters = SkyrimNet_Whipped_Engine.SyncCasters()
        int i = 0
        while i < codes.Length
            int code = codes[i]
            Trace("OnUpdate", "[dbg] code:" + code + " kind:" + (code / 10000) + " source:" + ((code / 1000) % 10) + " percent:" + (code % 1000) \
                + " target:" + NameOf(targets[i]) + " caster:" + NameOf(casters[i])) ; DEBUG-WHIP
            NarrateSourceHeal(code / 10000, (code / 1000) % 10, code % 1000, casters[i], targets[i])
            i += 1
        endwhile
    endif
    NarrateHealedActors() ; parts healed by health conversion this tick
    if SkyrimNet_Whipped_Engine.TrackedCount() > 0
        RegisterForSingleUpdate(SYNC_INTERVAL)
    endif
EndEvent

; Fully healed (the last healing narration) is always a DirectNarration while narration is on;
; start, pain gone and stopped go through the usual gate, otherwise an event.
; The healed actor speaks, toward the caster when there is one.
Function NarrateSourceHeal(int kind, int source, int percent, Actor caster, Actor target)
    if target == None
        return
    endif
    String msg = SourceHealText(kind, source, percent, caster, target)
    Actor toward = None
    if caster != target
        toward = caster
    endif
    if kind == HEAL_FULL
        ClearHealBatch(target) ; this narration covers the parts still pending
    endif
    if narration_enabled && (kind == HEAL_FULL || (!TooFar(target) && NarrationAllowed(target)))
        Trace("NarrateSourceHeal", "narration kind:" + kind + ": " + msg)
        StorageUtil.SetFloatValue(target, KEY_LAST_NARRATION, Utility.GetCurrentRealTime())
        DirectNarrate(msg, target, toward)
    else
        Trace("NarrateSourceHeal", "event: " + msg)
        SkyrimNetApi.RegisterEvent("whip_heal", msg, target, toward)
    endif
EndFunction

; Wording lives in prompts/helpers/healing.prompt; percent is turned into words there (no numbers in narration).
; kind and source go as numbers: Papyrus re-cases string literals ("Start", "SPELL").
String Function SourceHealText(int kind, int source, int percent, Actor caster, Actor target)
    String name = target.GetDisplayName()
    String fallback = name + "'s whip wounds are healing."
    if kind == HEAL_PAIN_GONE
        fallback = "The pain from " + name + "'s whip wounds is gone, but the healing continues."
    elseif kind == HEAL_FULL
        fallback = name + "'s whip wounds are now fully healed."
    elseif kind == HEAL_STOPPED
        fallback = "The healing of " + name + "'s whip wounds stops before it is done."
    endif
    String caster_name = ""
    if caster != None
        caster_name = caster.GetDisplayName()
    endif
    ; 0/1, not "true"/"false": Papyrus interns strings case-insensitively, so those arrive as TRUE/False (invalid JSON).
    int female = 0
    if target.GetLeveledActorBase().GetSex() == 1
        female = 1
    endif
    int is_target = 0
    if caster == target
        is_target = 1
    endif
    String json = "{\"kind\":" + kind + ",\"name\":" + JsonString(name) + ",\"female\":" + female \
        + ",\"caster\":" + JsonString(caster_name) + ",\"caster_is_target\":" + is_target \
        + ",\"source\":" + source + ",\"percent\":" + percent + "}"
    return RenderHealPrompt(json, fallback)
EndFunction

String Function JsonString(String text) global
    String out = ""
    int i = 0
    int n = StringUtil.GetLength(text)
    while i < n
        String c = StringUtil.GetNthChar(text, i)
        if c == "\"" || c == "\\"
            out += "\\"
        endif
        out += c
        i += 1
    endwhile
    return "\"" + out + "\""
EndFunction

; helpers/healing.prompt with the "heal" JSON. RenderTemplate binds its variable as a string, so it only
; loads the template (a raw block, returned unrendered); ParseString then renders it with heal parsed as JSON.
; Empty, error or leftover "{{" / "{%" -> fallback so SkyrimNet errors are never narrated.
String Function RenderHealPrompt(String json, String fallback)
    String source = SkyrimNetApi.RenderTemplate("helpers/healing", "", "")
    if source == "" || StringUtil.Substring(source, 0, 5) == "Error" || StringUtil.Find(source, "inja.exception") >= 0
        Trace("RenderHealPrompt", "--- template load failed json:" + json + " result:" + source)
        return fallback
    endif
    String result = SkyrimNetApi.ParseString(source, "heal", json)
    if result == "" || StringUtil.Substring(result, 0, 5) == "Error" || StringUtil.Find(result, "inja.exception") >= 0 \
        || StringUtil.Find(result, "{{") >= 0 || StringUtil.Find(result, "{%") >= 0
        Trace("RenderHealPrompt", "--- render failed json:" + json + " result:" + result)
        return fallback
    endif
    Trace("RenderHealPrompt", "json:" + json + " result:" + result)
    return result
EndFunction

String Function NameOf(Actor akActor) global
    if akActor == None
        return "Someone"
    endif
    return akActor.GetDisplayName()
EndFunction

;----------------------------------------------------------------------------------------------------
; Decorator skyrimnet_whipped_welts(uuid): SkyrimNet_Whipped_Engine.GetStateJson
;   {"parts":[{"part":"left arm","region":"arm","stage":"medium","text":8,"damage":5,"healing":0},...]}
;----------------------------------------------------------------------------------------------------
String Function Welts_Decorator(Actor akActor) global
    if akActor == None
        return "{\"parts\":[]}"
    endif
    return SkyrimNet_Whipped_Engine.GetStateJson(akActor)
EndFunction
