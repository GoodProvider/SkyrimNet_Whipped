Scriptname SkyrimNet_Whipped_PlayerRef extends ReferenceAlias

SkyrimNet_Whipped_Main Property main Auto

Event OnInit()
    OnPlayerLoadGame() ; new game: run setup without waiting for a reload
EndEvent

Event OnPlayerLoadGame()
    main.Setup()
EndEvent
