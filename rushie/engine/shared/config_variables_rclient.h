#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif
//Auto change
MACRO_CONFIG_STR(PlayerClanNoDummy, player_clan_no_dummy, 12, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Thats is clan when dummy not connected")
MACRO_CONFIG_STR(PlayerClanWithDummy, player_clan_with_dummy, 12, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Thats is clan when dummy connected")
MACRO_CONFIG_INT(PlayerClanAutoChange, player_clan_auto_change, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change clan when dummy connected")

//Chat
MACRO_CONFIG_INT(RiShowBlockedWordInConsole, ri_show_blocked_word_in_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show blocked word with regex in console")
MACRO_CONFIG_COL(RiBlockedWordConsoleColor, ri_blocked_word_console_color, 0x99ffff, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of blocked word messages in console")
MACRO_CONFIG_INT(RiEnableCensorList, ri_enable_censor_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable word block list")
MACRO_CONFIG_INT(RiMultipleReplacementChar, ri_multiple_replacement_char, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Multiple replacement char on blocked word len")
MACRO_CONFIG_STR(RiBlockedContentReplacementChar, ri_blocked_content_replacement_char, 64, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Character used to replace blocked content")
MACRO_CONFIG_STR(RiRegexPlayerWhitelist, ri_regex_player_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat filer whitelist")
MACRO_CONFIG_INT(RiFilterChangeWholeWord, ri_filter_change_whole_word, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change whole word to replacement character")
MACRO_CONFIG_STR(RiBlockedContentPartialReplacementChar, ri_blocked_content_partial_replacement_char, 64, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Character used to replace partial blocked content")

//Scoreboard
MACRO_CONFIG_INT(RiScoreboardFriendMark, ri_scoreboard_friendmark, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show friend mark (♥) in scoreboard")
MACRO_CONFIG_INT(RiScoreboardAlwaysShowQuickActions, ri_scoreboard_always_show_quick_actions, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show quick actions in scoreboard always")
MACRO_CONFIG_INT(RiScoreboardSortById, ri_scoreboard_sort_by_id, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sort scoreboard by id instead nickname")
MACRO_CONFIG_INT(RiScoreboardRclientIndicatorSize, ri_scoreboard_rclient_indicator_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Size of rclient indicator icons in scoreboard")
MACRO_CONFIG_INT(RiScoreboardShowRclientIndicator, ri_scoreboard_show_rclient_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show rclient mark in scoreboard")


//Lasers
MACRO_CONFIG_INT(RiLaserGlowIntensity, ri_laser_ench, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "")
MACRO_CONFIG_INT(RiBetterLasers, ri_better_laser, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "")

//Tracker
MACRO_CONFIG_INT(RiShowLastPosHud, ri_show_last_pos_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show last pos in hud target")
MACRO_CONFIG_INT(RiChangeTargetColorWhenXTargetEqualXPlayer, ri_change_target_color_when_x_equal, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change color pos x hud target to green when x target = x player")
MACRO_CONFIG_INT(RiChangePlayerColorWhenXTargetEqualXPlayer, ri_change_player_color_when_x_equal, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change color pos x hud player to green when x target = x player")

//Others pages
MACRO_CONFIG_INT(RiRClientSettingsTabs, ri_rclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")

//Voice
MACRO_CONFIG_INT(RiVoiceEnable, ri_voice_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable external voice chat")
MACRO_CONFIG_INT(RiVoiceProtocolVersion, ri_voice_protocol_version, 3, 1, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice protocol version for relay compatibility")
MACRO_CONFIG_STR(RiVoiceServer, ri_voice_server, 128, "server.rushie-client.ru:9987", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice server address host:port")
MACRO_CONFIG_STR(RiVoiceAudioBackend, ri_voice_audio_backend, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice audio backend (SDL audio driver name, empty = auto)")
MACRO_CONFIG_STR(RiVoiceInputDevice, ri_voice_input_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice input device name (empty = default)")
MACRO_CONFIG_STR(RiVoiceOutputDevice, ri_voice_output_device, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice output device name (empty = default)")
MACRO_CONFIG_INT(RiVoiceStereo, ri_voice_stereo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice output stereo (0=mono, 1=stereo)")
MACRO_CONFIG_INT(RiVoiceStereoWidth, ri_voice_stereo_width, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice stereo width (percent)")
MACRO_CONFIG_STR(RiVoiceToken, ri_voice_token, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice relay token (optional)")
MACRO_CONFIG_INT(RiVoiceGroupMode, ri_voice_group_mode, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice group mode (0=all/all, 1=group/group, 2=all->group, 3=group->all)")
MACRO_CONFIG_INT(RiVoiceFilterEnable, ri_voice_filter_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable voice HPF/compressor/limiter")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressEnable, ri_voice_noise_suppress_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable RNNoise voice suppressor")
MACRO_CONFIG_INT(RiVoiceNoiseSuppressStrength, ri_voice_noise_suppress_strength, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "RNNoise suppressor strength (percent)")
MACRO_CONFIG_INT(RiVoiceCompThreshold, ri_voice_comp_threshold, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice compressor threshold (percent)")
MACRO_CONFIG_INT(RiVoiceCompRatio, ri_voice_comp_ratio, 25, 10, 80, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice compressor ratio (x10)")
MACRO_CONFIG_INT(RiVoiceCompAttackMs, ri_voice_comp_attack_ms, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice compressor attack (ms)")
MACRO_CONFIG_INT(RiVoiceCompReleaseMs, ri_voice_comp_release_ms, 200, 10, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice compressor release (ms)")
MACRO_CONFIG_INT(RiVoiceCompMakeup, ri_voice_comp_makeup, 160, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice compressor makeup gain (percent)")
MACRO_CONFIG_INT(RiVoiceLimiter, ri_voice_limiter, 50, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice limiter level (percent)")
MACRO_CONFIG_INT(RiVoiceRadius, ri_voice_radius, 50, 1, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice radius in tiles")
MACRO_CONFIG_INT(RiVoiceVolume, ri_voice_volume, 100, 0, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice volume in percent")
MACRO_CONFIG_INT(RiVoiceMicVolume, ri_voice_mic_volume, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice microphone volume in percent")
MACRO_CONFIG_INT(RiVoiceMicMute, ri_voice_mic_mute, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute microphone capture")
MACRO_CONFIG_INT(RiVoiceTestMode, ri_voice_test_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice test mode (0=off, 1=local, 2=server)")
MACRO_CONFIG_INT(RiVoiceVadEnable, ri_voice_vad_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable voice activation (VAD)")
MACRO_CONFIG_INT(RiVoiceVadThreshold, ri_voice_vad_threshold, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice activation threshold (percent)")
MACRO_CONFIG_INT(RiVoiceVadReleaseDelayMs, ri_voice_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay after voice activation ends (ms)")
MACRO_CONFIG_INT(RiVoiceIgnoreDistance, ri_voice_ignore_distance, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ignore distance attenuation for voice chat")
MACRO_CONFIG_INT(RiVoiceGroupGlobal, ri_voice_group_global, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hear group members everywhere (ignore distance for same group)")
MACRO_CONFIG_INT(RiVoiceVisibilityMode, ri_voice_visibility_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice visibility mode (0=visible players, 1=team members, 2=all)")
MACRO_CONFIG_INT(RiVoiceListMode, ri_voice_list_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice list mode (0=off, 1=whitelist, 2=blacklist)")
MACRO_CONFIG_STR(RiVoiceWhitelist, ri_voice_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice whitelist (comma-separated names)")
MACRO_CONFIG_STR(RiVoiceBlacklist, ri_voice_blacklist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice blacklist (comma-separated names)")
MACRO_CONFIG_STR(RiVoiceMute, ri_voice_mute, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice mute list (comma-separated names, always muted)")
MACRO_CONFIG_INT(RiVoiceHearVad, ri_voice_hear_vad, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hear players using voice activation")
MACRO_CONFIG_STR(RiVoiceVadAllow, ri_voice_vad_allow, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice activation allow list (comma-separated names)")
MACRO_CONFIG_STR(RiVoiceNameVolumes, ri_voice_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice per-name volume list (name=percent)")
MACRO_CONFIG_INT(RiVoiceShowIndicator, ri_voice_show_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice activity icon in nameplates")
MACRO_CONFIG_INT(RiVoiceIndicatorAboveSelf, ri_voice_indicator_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice activity icon above yourself")
MACRO_CONFIG_INT(RiVoiceShowPing, ri_voice_show_ping, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice ping near HUD indicator")
MACRO_CONFIG_INT(RiVoiceDebug, ri_voice_debug, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Log voice chat debug info to console")
MACRO_CONFIG_INT(RiVoiceShowWhenActive, ri_voice_show_when_active, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice chat indicator when active")
MACRO_CONFIG_INT(RiVoiceOffNonActive, ri_voice_off_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Off Voice when window not active (0-working always, 1-off nonactive")
MACRO_CONFIG_INT(RiVoicePttReleaseDelayMs, ri_voice_ptt_release_delay_ms, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay after releasing voice PTT (ms)")
MACRO_CONFIG_INT(RiVoiceHearOnSpecPos, ri_voice_hear_on_spec_pos, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hear peoples in your spec pos")
MACRO_CONFIG_INT(RiVoiceHearPeoplesInSpectate, ri_voice_hear_peoples_in_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hear observers (inactive players, not /spec)")
MACRO_CONFIG_INT(RiVoiceShowOverlay, ri_voice_show_overlay, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show voice overlay")

//Controls
MACRO_CONFIG_INT(RiToggle45degrees, ri_toggle_45_degrees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle 45 degrees bind or not")
MACRO_CONFIG_INT(Ri45degreesEcho, ri_45_degrees_echo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "45 degrees echo")
MACRO_CONFIG_INT(RiToggleSmallSens, ri_toggle_small_sens, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle small sens bind or not")
MACRO_CONFIG_INT(RiSmallSensEcho, ri_small_sens_echo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Small sens echo")

//Hud
MACRO_CONFIG_INT(RiShowMilliSecondsTimer, ri_show_milliseconds_timer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show milliseconds in game timer")
MACRO_CONFIG_INT(RiHeartSize, ri_heart_size, 75, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Edit size of friend heart")

//Dummy
MACRO_CONFIG_INT(RiShowhudDummyPosition, ri_showhud_dummy_position, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show ingame HUD (Dummy Position)")
MACRO_CONFIG_INT(RiShowLastPosHudDummy, ri_show_last_pos_hud_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show last pos in hud dummy")
MACRO_CONFIG_INT(RiChangeDummyColorWhenXDummyEqualXPlayer, ri_change_dummy_color_when_x_equal_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change color pos x hud dummy to green when x dummy = x player")
MACRO_CONFIG_INT(RiChangePlayerColorWhenXDummyEqualXPlayer, ri_change_player_color_when_x_equal_dummy, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change color pos x hud player to green when x dummy = x player")
MACRO_CONFIG_INT(RiAdvancedShowhudDummyActions, ri_advanced_showhud_dummy_actions, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show advanced ingame HUD (Advanced Dummy Actions)")

//Nameplates
MACRO_CONFIG_INT(RiShowFire, ri_show_fire, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Fire in nameplates (0=off, 1 = only dummy, 2 = both, 3 = only your own)")
MACRO_CONFIG_INT(RiFireDetectionSize, ri_fire_detection_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Size of fire detection icons")
MACRO_CONFIG_INT(RiShowHook, ri_show_hook, 0, 0, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Hook in nameplates (0=off, 1 = other players', 2 = everyone, 3 = only your own)")
MACRO_CONFIG_INT(RiHookDetectionSize, ri_hook_detection_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Size of hook detection icons")
MACRO_CONFIG_STR(RiNamePlateScheme, ri_nameplate_scheme, 32, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "The order in which to show nameplate items (p=ping i=ignore m=ID n=name c=clan d=direction f=friend h=hook r=reason s=skin H=HookName F=FireName l=newline)")
MACRO_CONFIG_INT(RiShowFireDynamic, ri_show_fire_dynamic, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Fire in nameplates will dynamically change pos")
MACRO_CONFIG_INT(RiShowHookDynamic, ri_show_fire_dynamic, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Hook in nameplates will dynamically change pos")
MACRO_CONFIG_INT(RiRclientIndicatorSize, ri_rclient_indicator_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Size of rclient indicator icons")
MACRO_CONFIG_INT(RiShowIndicatorDynamic, ri_show_indicator_dynamic, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rclient indicator in nameplates will dynamically change pos")
MACRO_CONFIG_INT(RiRclientIndicatorAboveSelf, ri_rclient_indicator_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Rclient indicator above you")

//Changed TClient
MACRO_CONFIG_INT(RiIndicatorTransparentOffset, ri_indicator_transparent_offset, 42, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "(16-128) Offset of indicator transparent position")
MACRO_CONFIG_INT(RiIndicatorTransparentOffsetMax, ri_indicator_transparent_offset_max, 100, 16, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "(16-128) Max indicator transparent offset for variable offset setting")
MACRO_CONFIG_INT(RiIndicatorTransparentToggle, ri_indicator_transparent_toggle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle player indicator transparent")
MACRO_CONFIG_INT(RiIndicatorTransparentMin, ri_indicator_transparent_min, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimal transparent when offset max reached")
MACRO_CONFIG_INT(RiFrozenHudPosX, ri_frozen_hud_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change frozen hud pos x")
MACRO_CONFIG_INT(RiFrozenHudWarlistTeamOnly, ri_frozen_hud_warlist_team_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen HUD only for players in warlist team group")
MACRO_CONFIG_INT(RiColorFreezeNoYourself, ri_color_freeze_no_yourself, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Don't use color skin in freeze for yourself")


// Chat Anim
MACRO_CONFIG_INT(RiChatAnim, ri_chat_anim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle for chat animation")
MACRO_CONFIG_INT(RiChatAnimMs, ri_chat_anim_ms, 300, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation long")

// Chat Bubbles
MACRO_CONFIG_INT(RiChatBubbles, ri_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles above players")
MACRO_CONFIG_INT(RiChatBubblesSelf, ri_chat_bubbles_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles above you")
MACRO_CONFIG_INT(RiChatBubblesDemo, ri_chat_bubbles_demo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles in demoplayer")
MACRO_CONFIG_INT(RiChatBubbleSize, ri_chat_bubble_size, 20, 20, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the chat bubble")
MACRO_CONFIG_INT(RiChatBubbleShowTime, ri_chat_bubble_showtime, 200, 200, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long to show the bubble for")
MACRO_CONFIG_INT(RiChatBubbleFadeOut, ri_chat_bubble_fadeout, 35, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades out")
MACRO_CONFIG_INT(RiChatBubbleFadeIn, ri_chat_bubble_fadein, 15, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "how long it fades in")

// RCON
MACRO_CONFIG_INT(RiPlaySounds, ri_play_sounds, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Plays sound when do command")

//Edge helper
MACRO_CONFIG_COL(RiEdgeInfoColorFreeze, ri_edge_info_color_freeze, 9930605, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeze color in edge info")
MACRO_CONFIG_COL(RiEdgeInfoColorKill, ri_edge_info_color_kill, 65461, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Kill color in edge info")
MACRO_CONFIG_COL(RiEdgeInfoColorSafe, ri_edge_info_color_safe, 5594535, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Safe color in edge info")
MACRO_CONFIG_INT(RiEdgeInfoCords, ri_edge_info_cords, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show upper panel of edge info")
MACRO_CONFIG_INT(RiEdgeInfoJump, ri_edge_info_jump, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show lower panel of edge info")
MACRO_CONFIG_INT(RiEdgeInfoPosX, ri_edge_info_pos_x, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change edge info pos x")
MACRO_CONFIG_INT(RiEdgeInfoPosY, ri_edge_info_pos_y, 56, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change edge info pos y")

//Menus
MACRO_CONFIG_INT(RiBrFilterPlayers, ri_br_filter_players, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only empty servers")
MACRO_CONFIG_INT(RiUiNewMenu, ri_ui_new_menu, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show new RClient's menu")
MACRO_CONFIG_INT(RiUiCustomBg, ri_ui_custom_bg, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "custom RClient's menu background")
MACRO_CONFIG_INT(RiUiShowTopBar, ri_ui_show_topbar, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "show RClient's menu topbar")
MACRO_CONFIG_INT(RiUiShowBottomBar, ri_ui_show_bottombar, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "show RClient's menu bottombar")
MACRO_CONFIG_INT(RiUiSkipOpenMenu, ri_ui_skip_open_menu, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Skip open new rclient's menu")
MACRO_CONFIG_COL(RiMenusSettingsColor, ri_menus_settings_color, 1073742079, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of rclient's settings menu")

//RClient Nameplates
MACRO_CONFIG_INT(RiShowRclientIndicator, ri_show_rclient_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Rclient player indicator in nameplates")
MACRO_CONFIG_STR(RiRclientIndicatorUsersUrl, ri_rclient_indicator_users_url, 256, "https://server.rushie-client.ru/users.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rclient indicator users URL")
MACRO_CONFIG_STR(RiRclientIndicatorTokenUrl, ri_rclient_indicator_token_url, 256, "https://server.rushie-client.ru/token", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Rclient indicator token URL")

//Config tab
MACRO_CONFIG_INT(RiUiShowRClient, ri_ui_show_rclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show RClient domain in Configs tab")

//Spectator
MACRO_CONFIG_INT(RiSpectatorMoveSpeed, ri_spectator_move_speed, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speed of spectator freeview movement")
MACRO_CONFIG_INT(RiSpectatorMoveEnable, ri_spectator_move_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable spectator movement binds")
MACRO_CONFIG_INT(RiSpectatorSortById, ri_spectator_sort_by_id, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sort spectator by id instead nickname")

//Effects
MACRO_CONFIG_INT(RiShowSparkleTrail, ri_show_sparkle_trail, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show sparkle trail from invicible in rcon")
MACRO_CONFIG_INT(RiShowAfkEmoteInMenu, ri_show_afk_emote_menu, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk emote when player in menu (only client)")
MACRO_CONFIG_INT(RiShowAfkTextureInMenu, ri_show_afk_texture_in_menu, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk texture when player in menu (only client)")
MACRO_CONFIG_INT(RiShowAfkEmoteInSpec, ri_show_afk_emote_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows afk emote when player in spec (only client)")
MACRO_CONFIG_INT(RiShowAfkTextureInSpec, ri_show_afk_texture_in_spec, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows spec texture when player in spec (only client)")
MACRO_CONFIG_INT(RiShowFrozenFlakes, ri_show_frozen_flakes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show frozen flakes in freeze")
MACRO_CONFIG_INT(RiShowHammerHit, ri_show_hammer_hit, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hammer hits(0-NoEffect, 1-Normal, 2-NoSound)")
MACRO_CONFIG_INT(RiPlayOnMoveNonInactive, ri_play_on_move_nonactive, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Play sound when someone moves u when u inactive")
MACRO_CONFIG_INT(RiSoundOnMoveNonInactive, ri_sound_on_move_nonactive, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Choose sound when inactive (0-WakeUp, 1-Grenade boom, 2-msg tag")

//Fastinput
MACRO_CONFIG_INT(RiFastInputVersion, ri_fast_input_version, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Choose fastinput version (0-Tater's old, 1-Tater's new)")
