// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Tcme, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Tcme, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Tcme, ScriptName, Len, Def, Save, Desc) ;
#endif

#if defined(CONF_FAMILY_WINDOWS)
#else
#endif




MACRO_CONFIG_INT(BcNameplateAsyncIndicator, bc_nameplate_async_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Shows an async indicator from 1/5 to 5/5 in your nameplate")
MACRO_CONFIG_INT(BcNameplateAsyncOffsetX, bc_nameplate_async_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the async indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateAsyncOffsetY, bc_nameplate_async_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the async indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateIgnoreOffsetX, bc_nameplate_ignore_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the ignore marker in nameplates")
MACRO_CONFIG_INT(BcNameplateIgnoreOffsetY, bc_nameplate_ignore_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the ignore marker in nameplates")
MACRO_CONFIG_INT(BcNameplateFriendOffsetX, bc_nameplate_friend_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the friend marker in nameplates")
MACRO_CONFIG_INT(BcNameplateFriendOffsetY, bc_nameplate_friend_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the friend marker in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIdInlineOffsetX, bc_nameplate_client_id_inline_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the inline client id in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIdInlineOffsetY, bc_nameplate_client_id_inline_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the inline client id in nameplates")
MACRO_CONFIG_INT(BcNameplateNameOffsetX, bc_nameplate_name_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the player name in nameplates")
MACRO_CONFIG_INT(BcNameplateNameOffsetY, bc_nameplate_name_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the player name in nameplates")
MACRO_CONFIG_INT(BcNameplateVoiceOffsetX, bc_nameplate_voice_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the voice icon in nameplates")
MACRO_CONFIG_INT(BcNameplateVoiceOffsetY, bc_nameplate_voice_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the voice icon in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIndicatorOffsetX, bc_nameplate_client_indicator_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the client indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIndicatorOffsetY, bc_nameplate_client_indicator_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the client indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateClanOffsetX, bc_nameplate_clan_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the clan line in nameplates")
MACRO_CONFIG_INT(BcNameplateClanOffsetY, bc_nameplate_clan_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the clan line in nameplates")
MACRO_CONFIG_INT(BcNameplateReasonOffsetX, bc_nameplate_reason_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the reason line in nameplates")
MACRO_CONFIG_INT(BcNameplateReasonOffsetY, bc_nameplate_reason_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the reason line in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIdSeparateOffsetX, bc_nameplate_client_id_separate_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the separate client id line in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIdSeparateOffsetY, bc_nameplate_client_id_separate_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the separate client id line in nameplates")
MACRO_CONFIG_INT(BcNameplateHookIconOffsetX, bc_nameplate_hook_icon_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the hook strength icon in nameplates")
MACRO_CONFIG_INT(BcNameplateHookIconOffsetY, bc_nameplate_hook_icon_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the hook strength icon in nameplates")
MACRO_CONFIG_INT(BcNameplateHookIdOffsetX, bc_nameplate_hook_id_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the hook strength id in nameplates")
MACRO_CONFIG_INT(BcNameplateHookIdOffsetY, bc_nameplate_hook_id_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the hook strength id in nameplates")
MACRO_CONFIG_INT(BcNameplateDirLeftOffsetX, bc_nameplate_dir_left_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the left direction icon in nameplates")
MACRO_CONFIG_INT(BcNameplateDirLeftOffsetY, bc_nameplate_dir_left_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the left direction icon in nameplates")
MACRO_CONFIG_INT(BcNameplateDirUpOffsetX, bc_nameplate_dir_up_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the up direction icon in nameplates")
MACRO_CONFIG_INT(BcNameplateDirUpOffsetY, bc_nameplate_dir_up_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the up direction icon in nameplates")
MACRO_CONFIG_INT(BcNameplateDirRightOffsetX, bc_nameplate_dir_right_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the right direction icon in nameplates")
MACRO_CONFIG_INT(BcNameplateDirRightOffsetY, bc_nameplate_dir_right_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the right direction icon in nameplates")



//Controls
MACRO_CONFIG_INT(BCPrevMouseMaxDistance45degrees, bc_prev_mouse_max_distance_45_degrees, 400, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Previous maximum cursor distance for 45 degrees")
MACRO_CONFIG_INT(BCPrevInpMousesens45degrees, bc_prev_inp_mousesens_45_degrees, 200, 1, 100000, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Previous mouse sensitivity for 45 degrees")
MACRO_CONFIG_INT(BCToggle45degrees, bc_toggle_45_degrees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle 45 degrees bind or not")
MACRO_CONFIG_INT(BCPrevInpMousesensSmallsens, bc_prev_inp_mousesens_small_sens, 200, 1, 100000, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Previous mouse sensitivity for small sens")
MACRO_CONFIG_INT(BCToggleSmallSens, bc_toggle_small_sens, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle small sens bind or not")
MACRO_CONFIG_INT(BcGoresMode, bc_gores_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Entity-like gores mode without bind")
MACRO_CONFIG_INT(BcGoresModeDisableIfWeapons, bc_gores_mode_disable_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable gores mode when holding shotgun, grenade or laser")
MACRO_CONFIG_INT(BcCrystalLaser, bc_crystal_laser, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render rifle and shotgun lasers with crystal shards and icy glow")
MACRO_CONFIG_INT(BcHookCombo, bc_hook_combo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hook combo popups with combo sounds")
MACRO_CONFIG_INT(BcHookComboMode, bc_hook_combo_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo trigger mode (0=hook, 1=hammer, 2=hook and hammer)")
MACRO_CONFIG_INT(BcHookComboResetTime, bc_hook_combo_reset_time, 1200, 100, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum time in ms between player hooks before combo restarts")
MACRO_CONFIG_INT(BcHookComboSoundVolume, bc_hook_combo_sound_volume, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo sound volume")
MACRO_CONFIG_INT(BcHookComboSize, bc_hook_combo_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo popup size")
MACRO_CONFIG_INT(BcMusicPlayer, bc_music_player, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Music Player HUD element")
MACRO_CONFIG_INT(BcMusicPlayerShowWhenPaused, bc_music_player_show_when_paused, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep Music Player visible while playback is paused")
MACRO_CONFIG_INT(BcMusicPlayerColorMode, bc_music_player_color_mode, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player color mode (0=static color, 1=cover blur color)")
MACRO_CONFIG_COL(BcMusicPlayerStaticColor, bc_music_player_static_color, 128, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Static color for the music player when static color mode is selected")
MACRO_CONFIG_INT(BcDisabledComponentsMaskLo, bc_disabled_components_mask_lo, 0, 0, 2147483647, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Low bitmask for disabled BestClient components")
MACRO_CONFIG_INT(BcDisabledComponentsMaskHi, bc_disabled_components_mask_hi, 0, 0, 2147483647, CFGFLAG_CLIENT | CFGFLAG_SAVE, "High bitmask for disabled BestClient components")

	// Cava HUD visualizer (system audio capture)
	MACRO_CONFIG_INT(BcCavaEnable, bc_cava_enable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Cava HUD visualizer (system output via loopback/monitor)")
	MACRO_CONFIG_INT(BcCavaCaptureDevice, bc_cava_capture_device, -1, -1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava capture device index (-1=auto)")
	MACRO_CONFIG_INT(BcCavaDockBottom, bc_cava_dock_bottom, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Dock Cava visualizer to bottom edge and stretch full width")
	MACRO_CONFIG_INT(BcCavaX, bc_cava_x, 250, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava HUD position X in canvas units (0..500)")
	MACRO_CONFIG_INT(BcCavaY, bc_cava_y, 236, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava HUD position Y in canvas units (0..300)")
MACRO_CONFIG_INT(BcCavaW, bc_cava_w, 160, 20, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava HUD width in canvas units (0..500)")
MACRO_CONFIG_INT(BcCavaH, bc_cava_h, 48, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava HUD height in canvas units (0..200)")
MACRO_CONFIG_INT(BcCavaBars, bc_cava_bars, 40, 4, 128, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Number of Cava frequency bars")
MACRO_CONFIG_INT(BcCavaFftSize, bc_cava_fft_size, 2048, 1024, 4096, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FFT size for Cava (1024/2048/4096)")
MACRO_CONFIG_INT(BcCavaLowHz, bc_cava_low_hz, 50, 20, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Low cutoff frequency in Hz for Cava")
MACRO_CONFIG_INT(BcCavaHighHz, bc_cava_high_hz, 16000, 1000, 20000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "High cutoff frequency in Hz for Cava")
MACRO_CONFIG_INT(BcCavaGain, bc_cava_gain, 120, 50, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava gain in percent")
MACRO_CONFIG_INT(BcCavaAttack, bc_cava_attack, 60, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava bar attack speed (1..100)")
MACRO_CONFIG_INT(BcCavaDecay, bc_cava_decay, 40, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cava bar decay speed (1..100)")
MACRO_CONFIG_COL(BcCavaColor, bc_cava_color, 0xFF968C80U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Bar color for Cava visualizer")
MACRO_CONFIG_INT(BcCavaBackground, bc_cava_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw background behind Cava visualizer")
MACRO_CONFIG_COL(BcCavaBackgroundColor, bc_cava_background_color, 0x66000000U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Background color for Cava visualizer")
MACRO_CONFIG_INT(BcMenuSfx, bc_menu_sfx, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable osu!lazer menu sound effects")
MACRO_CONFIG_INT(BcMenuSfxVolume, bc_menu_sfx_volume, 70, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "osu!lazer menu sound effects volume")
MACRO_CONFIG_INT(BcMenuMediaBackground, bc_menu_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in offline menus")
MACRO_CONFIG_INT(BcGameMediaBackground, bc_game_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in game background rendering")
MACRO_CONFIG_STR(BcMenuMediaBackgroundPath, bc_menu_media_background_path, IO_MAX_PATH_LENGTH, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Path to the custom menu media background file")
MACRO_CONFIG_INT(BcGameMediaBackgroundOffset, bc_game_media_background_offset, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the custom media background is fixed to the map when rendering the in-game background")
MACRO_CONFIG_INT(BcShopAutoSet, bc_shop_auto_set, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply downloaded shop assets automatically")

// Optimizer
MACRO_CONFIG_INT(BcOptimizer, bc_optimizer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable optimizer features")
MACRO_CONFIG_INT(BcOptimizerDisableParticles, bc_optimizer_disable_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable rendering/updating all particles")
MACRO_CONFIG_INT(BcOptimizerFpsFog, bc_optimizer_fps_fog, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cull non-map rendering outside a distance limit around the camera")
MACRO_CONFIG_INT(BcOptimizerDdnetPriorityHigh, bc_optimizer_ddnet_priority_high, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Set DDNet process priority to High while enabled")
MACRO_CONFIG_INT(BcOptimizerDiscordPriorityBelowNormal, bc_optimizer_discord_priority_below_normal, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Set Discord process priority to Below Normal while enabled")
MACRO_CONFIG_INT(BcOptimizerFpsFogMode, bc_optimizer_fps_fog_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog mode (0=manual radius tiles, 1=by zoom percent)")
MACRO_CONFIG_INT(BcOptimizerFpsFogRadiusTiles, bc_optimizer_fps_fog_radius_tiles, 40, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog manual radius in tiles (tile=32 units)")
MACRO_CONFIG_INT(BcOptimizerFpsFogZoomPercent, bc_optimizer_fps_fog_zoom_percent, 70, 10, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog visible area percent in zoom mode")
MACRO_CONFIG_INT(BcOptimizerFpsFogRenderRect, bc_optimizer_fps_fog_render_rect, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render an outline rectangle showing the FPS fog area")
MACRO_CONFIG_INT(BcOptimizerFpsFogCullMapTiles, bc_optimizer_fps_fog_cull_map_tiles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cull map tile rendering outside the FPS fog area")

// Focus Mode Settings
MACRO_CONFIG_INT(ClFocusMode, p_focus_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable focus mode to minimize visual distractions")
MACRO_CONFIG_INT(ClFocusModeHideNames, p_focus_mode_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide player names in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideEffects, p_focus_mode_hide_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide visual effects in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideHud, p_focus_mode_hide_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide HUD in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideSongPlayer, p_focus_mode_hide_song_player, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide song player in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideUI, p_focus_mode_hide_ui, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide unnecessary UI elements in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideChat, p_focus_mode_hide_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide chat in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideScoreboard, p_focus_mode_hide_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide scoreboard in focus mode")
//Effects controls
MACRO_CONFIG_INT(ClFreezeSnowFlakes, p_effect_freeze_snowflakes, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "toggles snowflakes effect")
MACRO_CONFIG_INT(ClHammerHitEffect, p_effect_hammerhit, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "toggles hammer hit effect")
MACRO_CONFIG_INT(ClHammerHitEffectSound, p_effect_sound_hammerhit, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "toggles hammer hit effect")
MACRO_CONFIG_INT(ClJumpEffect, p_effect_jump, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "toggles hammer hit effect")
MACRO_CONFIG_INT(ClJumpEffectSound, p_effect_sound_jump, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "toggles jump effect sound")






MACRO_CONFIG_INT(BcFastInputMode, bc_fast_input_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input mode (0 = fast input, 1 = low delta)")
MACRO_CONFIG_INT(BcFastInputLowDelta, bc_fast_input_low_delta, 0, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fast input amount for low delta mode in 0.01 ticks (0-5)")
MACRO_CONFIG_INT(BcLowDeltaOthers, bc_low_delta_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply low delta to other tees")

MACRO_CONFIG_INT(BcEmoticonShadow, bc_emoticon_shadow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw shadow behind emoticons")
MACRO_CONFIG_INT(BcChatSaveDraft, bc_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep unfinished chat input when closing chat")

// Revert Variables






// Regex chat matching

// Misc visual

// MACRO_CONFIG_INT(TcRenderNameplateSpec, tc_render_nameplate_spec, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render nameplates when spectating")

// Profiles



// War List






// Voting
MACRO_CONFIG_INT(BcAutoTeamLock, bc_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically lock your team after joining it")
MACRO_CONFIG_INT(BcAutoTeamLockDelay, bc_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay before auto-locking team after joining, in seconds")
MACRO_CONFIG_INT(BcAutoDummyJoin, bc_auto_dummy_join, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically sync main and dummy into the same race team")


// Translate
MACRO_CONFIG_STR(BcTranslateIncomingSource, bc_translate_incoming_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language for incoming chat translation (use auto to detect)")
MACRO_CONFIG_STR(BcTranslateOutgoingSource, bc_translate_outgoing_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language for your outgoing chat translation (use auto to detect)")
MACRO_CONFIG_STR(BcTranslateOutgoingTarget, bc_translate_outgoing_target, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Target language for your outgoing chat translation")

// Pets


// Flags
MACRO_CONFIG_INT(BcBestClientSettingsTabs, bc_bestclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable settings tabs")
MACRO_CONFIG_INT(BcSettingsLayout, bc_settings_layout, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Menu layout (0 = new, 1 = old)")

// Volleyball

// Mod


// Custom Communities

// Discord RPC
MACRO_CONFIG_INT(ClDiscordRPC, ec_discord_rpc, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle Discord Rpc (no restart needed)")
MACRO_CONFIG_INT(ClDiscordMapStatus, ec_discord_map_status, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show What Map you're on")


// Configs tab UI
MACRO_CONFIG_INT(BcUiShowBestClient, bc_ui_show_bestclient, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show BestClient domain in Configs tab")

// Dummy Info
MACRO_CONFIG_INT(BcShowhudDummyCoordIndicator, bc_showhud_dummy_coord_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player-below indicator in ingame HUD")
MACRO_CONFIG_COL(BcShowhudDummyCoordIndicatorColor, bc_showhud_dummy_coord_indicator_color, 255, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player-below indicator color")
MACRO_CONFIG_COL(BcShowhudDummyCoordIndicatorSameHeightColor, bc_showhud_dummy_coord_indicator_same_height_color, 65407, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player-below indicator color when aligned vertically")


// Best Client
MACRO_CONFIG_INT(BcLoadscreenToggle, bc_loadscreentoggle, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle loading screen on/off")

// Vusials

// Magic Particles
MACRO_CONFIG_INT(BcMagicParticles, bc_magic_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle magic particles")
MACRO_CONFIG_INT(BcMagicParticlesRadius, bc_magic_particles_radius, 10, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Radius of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesSize, bc_magic_particles_size, 8, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesAlphaDelay, bc_magic_particles_alpha_delay, 3, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha delay of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesType, bc_magic_particles_type, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Type of magic particles. 1 = SLICE, 2 = BALL, 3 = SMOKE, 4 = SHELL")
MACRO_CONFIG_INT(BcMagicParticlesCount, bc_magic_particles_count, 10, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Count of magic particles")


// i'll add animations later
// Animations
MACRO_CONFIG_INT(BcAnimations, bc_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimation, bc_module_ui_reveal_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle module settings reveal animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimationMs, bc_module_ui_reveal_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Module settings reveal time (in ms)")
MACRO_CONFIG_INT(BcIngameMenuAnimation, bc_ingame_menu_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle ingame ESC menu animation")
MACRO_CONFIG_INT(BcIngameMenuAnimationMs, bc_ingame_menu_animation_ms, 220, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ingame ESC menu animation time (in ms)")

// Chat Animation
MACRO_CONFIG_INT(BcChatAnimation, bc_chat_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat animation")
MACRO_CONFIG_INT(BcChatAnimationMs, bc_chat_animation_ms, 300, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation time (in ms)")
MACRO_CONFIG_INT(BcChatOpenAnimation, bc_chat_open_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat open animation")
MACRO_CONFIG_INT(BcChatOpenAnimationMs, bc_chat_open_animation_ms, 220, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat open animation time (in ms)")
MACRO_CONFIG_INT(BcChatTypingAnimation, bc_chat_typing_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat typing animation")
MACRO_CONFIG_INT(BcChatTypingAnimationMs, bc_chat_typing_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat typing animation time (in ms)")
MACRO_CONFIG_INT(BcChatAnimationType, bc_chat_animation_type, 3, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation type")

// Chat Bubbles
MACRO_CONFIG_INT(BcChatBubbles, bc_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle Chatbubbles")
MACRO_CONFIG_INT(BcChatBubblesSelf, bc_chat_bubbles_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles above you")
MACRO_CONFIG_INT(BcChatBubblesDemo, bc_chat_bubbles_demo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles in demoplayer")
MACRO_CONFIG_INT(BcChatBubbleSize, bc_chat_bubble_size, 20, 20, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the chat bubble")
MACRO_CONFIG_INT(BcChatBubbleShowTime, bc_chat_bubble_showtime, 200, 200, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long to show the bubble for")
MACRO_CONFIG_INT(BcChatBubbleFadeOut, bc_chat_bubble_fadeout, 35, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades out")
MACRO_CONFIG_INT(BcChatBubbleFadeIn, bc_chat_bubble_fadein, 15, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades in")

// Client Indicator
MACRO_CONFIG_INT(BcClientIndicator, bc_client_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle client indicator")
MACRO_CONFIG_INT(DbgClientIndicator, dbg_client_indicator, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Debug logging for BestClient indicator (1=verbose, 2=dump all UDP packet bytes sent/received)")

MACRO_CONFIG_INT(BcClientIndicatorInNamePlate, bc_client_indicator_in_name_plate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator in name plate")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateAboveSelf, bc_client_indicator_in_name_plate_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator above self")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateSize, bc_client_indicator_in_name_plate_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Client indicator in name plate size")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateDynamic, bc_client_indicator_in_name_plate_dynamic, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator in nameplates will dynamically change pos")

MACRO_CONFIG_INT(BcClientIndicatorInScoreboard, bc_client_indicator_in_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator in name plate")
MACRO_CONFIG_INT(BcClientIndicatorInSoreboardSize, bc_client_indicator_in_scoreboard_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Client indicator in name plate size")

MACRO_CONFIG_STR(BcClientIndicatorServerAddress, bc_client_indicator_server_address, 256, "150.241.70.188:8778", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator UDP presence server")
MACRO_CONFIG_STR(BcClientIndicatorBrowserUrl, bc_client_indicator_browser_url, 256, "https://150.241.70.188:8779/users.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator browser JSON URL")
MACRO_CONFIG_STR(BcClientIndicatorTokenUrl, bc_client_indicator_token_url, 256, "https://150.241.70.188:8779/token.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator token bootstrap URL")
MACRO_CONFIG_STR(BcClientIndicatorSharedToken, bc_client_indicator_shared_token, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator shared token for signed UDP packets")
MACRO_CONFIG_INT(BrFilterBestclient, br_filter_bestclient, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter out servers with no BestClient users")

// Camera drift
MACRO_CONFIG_INT(BcCameraDrift, bc_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth camera drift that lags behind player movement")  
MACRO_CONFIG_INT(BcCameraDriftAmount, bc_camera_drift_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of camera drift (1 = minimal drift, 200 = maximum drift)")  
MACRO_CONFIG_INT(BcCameraDriftSmoothness, bc_camera_drift_smoothness, 20, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of camera drift (1 = near instant, 20 = very smooth)")
MACRO_CONFIG_INT(BcCameraDriftReverse, bc_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse camera drift direction (camera drifts opposite to movement)")
MACRO_CONFIG_INT(BcDynamicFov, bc_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Increase FOV dynamically based on movement speed")
MACRO_CONFIG_INT(BcDynamicFovAmount, bc_dynamic_fov_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of dynamic FOV boost (1 = minimal boost, 200 = maximum boost)")
MACRO_CONFIG_INT(BcDynamicFovSmoothness, bc_dynamic_fov_smoothness, 20, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of dynamic FOV boost (1 = near instant, 20 = very smooth)")
MACRO_CONFIG_INT(BcCinematicCamera, bc_cinematic_camera, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth cinematic camera movement in spectator freeview")

// Afterimage
MACRO_CONFIG_INT(BcAfterimage, bc_afterimage, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render afterimage layers of your tee")
MACRO_CONFIG_INT(BcAfterimageFrames, bc_afterimage_frames, 6, 2, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How many previous frames to keep for the afterimage")
MACRO_CONFIG_INT(BcAfterimageAlpha, bc_afterimage_alpha, 40, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum alpha of afterimage layers (1-100)")
MACRO_CONFIG_INT(BcAfterimageSpacing, bc_afterimage_spacing, 18, 1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Distance between afterimage samples")

// Speedrun Timer
MACRO_CONFIG_INT(BcSpeedrunTimer, bc_speedrun_timer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer")
MACRO_CONFIG_INT(BcSpeedrunTimerTime, bc_speedrun_timer_time, 0, 0, 9999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer time (MMSS format)")
MACRO_CONFIG_INT(BcSpeedrunTimerHours, bc_speedrun_timer_hours, 0, 0, 99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer hours")
MACRO_CONFIG_INT(BcSpeedrunTimerMinutes, bc_speedrun_timer_minutes, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer minutes")
MACRO_CONFIG_INT(BcSpeedrunTimerSeconds, bc_speedrun_timer_seconds, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer seconds")
MACRO_CONFIG_INT(BcSpeedrunTimerMilliseconds, bc_speedrun_timer_milliseconds, 0, 0, 999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer milliseconds")
MACRO_CONFIG_INT(BcSpeedrunTimerAutoDisable, bc_speedrun_timer_auto_disable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable speedrun timer automatically when time ends")

// Admin panel
MACRO_CONFIG_INT(BcAdminPanelAutoScroll, bc_adminpanel_autoscroll, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto-scroll logs in admin panel")
MACRO_CONFIG_INT(BcAdminPanelRememberTab, bc_adminpanel_remember_tab, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Remember last active admin panel tab")
MACRO_CONFIG_INT(BcAdminPanelLastTab, bc_adminpanel_last_tab, 0, 0, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Last active admin panel tab")
MACRO_CONFIG_INT(BcAdminPanelDisableAnim, bc_adminpanel_disable_anim, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable admin panel animations")
MACRO_CONFIG_INT(BcAdminPanelScale, bc_adminpanel_scale, 100, 80, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel scale in percent")
MACRO_CONFIG_INT(BcAdminPanelLogLines, bc_adminpanel_log_lines, 200, 50, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum lines to keep in admin panel logs")
MACRO_CONFIG_COL(BcAdminPanelBgColor, bc_adminpanel_bg_color, 0x8C000000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Admin panel background color")
MACRO_CONFIG_COL(BcAdminPanelTabInactiveColor, bc_adminpanel_tab_inactive_color, 0xCC00002E, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Admin panel inactive tab color")
MACRO_CONFIG_COL(BcAdminPanelTabActiveColor, bc_adminpanel_tab_active_color, 0xE6000052, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Admin panel active tab color")
MACRO_CONFIG_COL(BcAdminPanelTabHoverColor, bc_adminpanel_tab_hover_color, 0xE600003D, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Admin panel hover tab color")
MACRO_CONFIG_STR(BcAdminFastAction0, bc_admin_fast_action0, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 1")
MACRO_CONFIG_STR(BcAdminFastAction1, bc_admin_fast_action1, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 2")
MACRO_CONFIG_STR(BcAdminFastAction2, bc_admin_fast_action2, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 3")
MACRO_CONFIG_STR(BcAdminFastAction3, bc_admin_fast_action3, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 4")
MACRO_CONFIG_STR(BcAdminFastAction4, bc_admin_fast_action4, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 5")
MACRO_CONFIG_STR(BcAdminFastAction5, bc_admin_fast_action5, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 6")
MACRO_CONFIG_STR(BcAdminFastAction6, bc_admin_fast_action6, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 7")
MACRO_CONFIG_STR(BcAdminFastAction7, bc_admin_fast_action7, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 8")
MACRO_CONFIG_STR(BcAdminFastAction8, bc_admin_fast_action8, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 9")
MACRO_CONFIG_STR(BcAdminFastAction9, bc_admin_fast_action9, 96, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Admin panel fast action command slot 10")

// Orbit Aura
MACRO_CONFIG_INT(BcOrbitAura, bc_orbit_aura, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle orbit aura around the local player")
MACRO_CONFIG_INT(BcOrbitAuraRadius, bc_orbit_aura_radius, 32, 8, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura radius")
MACRO_CONFIG_INT(BcOrbitAuraParticles, bc_orbit_aura_particles, 14, 2, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura particle count")
MACRO_CONFIG_INT(BcOrbitAuraAlpha, bc_orbit_aura_alpha, 70, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura alpha")
MACRO_CONFIG_INT(BcOrbitAuraSpeed, bc_orbit_aura_speed, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura speed")
MACRO_CONFIG_INT(BcOrbitAuraIdle, bc_orbit_aura_idle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show orbit aura after standing idle")
MACRO_CONFIG_INT(BcOrbitAuraIdleTimer, bc_orbit_aura_idle_timer, 2, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Idle delay before orbit aura appears in seconds")

// 3D Particles
MACRO_CONFIG_INT(Bc3dParticles, bc_3d_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesType, bc_3d_particles_type, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Type of 3D particles. 1 = Cube, 2 = Heart, 3 = Mixed")
MACRO_CONFIG_INT(Bc3dParticlesCount, bc_3d_particles_count, 60, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Count of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesSizeMin, bc_3d_particles_size_min, 3, 2, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimum size of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesSizeMax, bc_3d_particles_size_max, 8, 2, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum size of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesSpeed, bc_3d_particles_speed, 18, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base speed of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesDepth, bc_3d_particles_depth, 300, 10, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Depth range for 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesAlpha, bc_3d_particles_alpha, 35, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha of 3D particles (1-100)")
MACRO_CONFIG_INT(Bc3dParticlesFadeInMs, bc_3d_particles_fade_in_ms, 400, 1, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade-in time in ms for 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesFadeOutMs, bc_3d_particles_fade_out_ms, 400, 1, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Fade-out time in ms for 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesPushRadius, bc_3d_particles_push_radius, 120, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player push radius for 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesPushStrength, bc_3d_particles_push_strength, 120, 0, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Player push strength for 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesCollide, bc_3d_particles_collide, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "3D particles collide with each other")
MACRO_CONFIG_INT(Bc3dParticlesViewMargin, bc_3d_particles_view_margin, 120, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Margin outside view before 3D particles disappear")
MACRO_CONFIG_INT(Bc3dParticlesColorMode, bc_3d_particles_color_mode, 1, 1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color mode for 3D particles. 1 = White, 2 = Random")
MACRO_CONFIG_COL(Bc3dParticlesColor, bc_3d_particles_color, 4294967295, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesGlow, bc_3d_particles_glow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable 3D particles glow")
MACRO_CONFIG_INT(Bc3dParticlesGlowAlpha, bc_3d_particles_glow_alpha, 35, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow alpha of 3D particles (1-100)")
MACRO_CONFIG_INT(Bc3dParticlesGlowOffset, bc_3d_particles_glow_offset, 2, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow offset for 3D particles")

// Lock cam
MACRO_CONFIG_INT(BcLockCamUseCustomZoom, bc_lock_cam_use_custom_zoom, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use custom zoom for lock cam")
MACRO_CONFIG_INT(BcLockCamZoom, bc_lock_cam_zoom, 85, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Lock cam zoom (percent)")

// Voice chat
MACRO_CONFIG_INT(BcVoiceChatEnable, bc_voice_chat_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable integrated voice chat")
MACRO_CONFIG_INT(BcVoiceChatActivationMode, bc_voice_chat_activation_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice activation mode (0 = automatic activation, 1 = push-to-talk)")
MACRO_CONFIG_INT(BcVoiceChatVolume, bc_voice_chat_volume, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice playback volume in percent")
MACRO_CONFIG_INT(BcVoiceChatMicGain, bc_voice_chat_mic_gain, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Microphone gain in percent")
MACRO_CONFIG_INT(BcVoiceChatBitrate, bc_voice_chat_bitrate, 96, 6, 96, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice Opus bitrate in kbps")
MACRO_CONFIG_INT(BcVoiceChatInputDevice, bc_voice_chat_input_device, -1, -1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice input device index (-1 = system default)")
MACRO_CONFIG_INT(BcVoiceChatOutputDevice, bc_voice_chat_output_device, -1, -1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice output device index (-1 = system default)")

// Debug
MACRO_CONFIG_INT(DbgCava, dbg_cava, 0, 0, 2, CFGFLAG_CLIENT, "Debug Cava/Audio visualizer (1=overlay, 2=overlay+logs)")
MACRO_CONFIG_INT(BcVoiceChatMicMuted, bc_voice_chat_mic_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute voice microphone")
MACRO_CONFIG_INT(BcVoiceChatHeadphonesMuted, bc_voice_chat_headphones_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute voice playback")
MACRO_CONFIG_INT(BcVoiceChatMicCheck, bc_voice_chat_mic_check, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable microphone check (local loopback)")
MACRO_CONFIG_INT(BcVoiceChatNameplateIcon, bc_voice_chat_nameplate_icon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show microphone icon in name plates for talking players")
MACRO_CONFIG_STR(BcVoiceChatServerAddress, bc_voice_chat_server_address, 128, "127.0.0.1:8777", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice server address")
MACRO_CONFIG_STR(BcVoiceChatMutedNames, bc_voice_chat_muted_names, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Comma-separated list of muted voice nicknames (case-insensitive)")
MACRO_CONFIG_STR(BcVoiceChatNameVolumes, bc_voice_chat_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Comma-separated list of voice nickname volumes in percent (name=value, case-insensitive)")

// World editor
MACRO_CONFIG_INT(BcWorldEditor, bc_world_editor, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable World Editor post processing")
MACRO_CONFIG_INT(BcWorldEditorPanelX, bc_world_editor_panel_x, 70, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor panel X position")
MACRO_CONFIG_INT(BcWorldEditorPanelY, bc_world_editor_panel_y, 70, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor panel Y position")
MACRO_CONFIG_INT(BcWorldEditorMotionBlur, bc_world_editor_motion_blur, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable World Editor motion blur")
MACRO_CONFIG_INT(BcWorldEditorMotionBlurStrength, bc_world_editor_motion_blur_strength, 35, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor motion blur strength")
MACRO_CONFIG_INT(BcWorldEditorMotionBlurResponse, bc_world_editor_motion_blur_response, 65, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor motion blur response")
MACRO_CONFIG_INT(BcWorldEditorBloom, bc_world_editor_bloom, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable World Editor bloom")
MACRO_CONFIG_INT(BcWorldEditorBloomIntensity, bc_world_editor_bloom_intensity, 40, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor bloom intensity")
MACRO_CONFIG_INT(BcWorldEditorBloomThreshold, bc_world_editor_bloom_threshold, 55, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor bloom threshold")
MACRO_CONFIG_INT(BcWorldEditorOutline, bc_world_editor_outline, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable World Editor outline")
MACRO_CONFIG_INT(BcWorldEditorOutlineIntensity, bc_world_editor_outline_intensity, 45, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor outline intensity")
MACRO_CONFIG_INT(BcWorldEditorOutlineThreshold, bc_world_editor_outline_threshold, 30, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "World Editor outline threshold")

// Graphics
MACRO_CONFIG_INT(BcCustomAspectRatioMode, bc_custom_aspect_ratio_mode, -1, -1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio mode (-1=legacy auto, 0=off, 1=preset, 2=custom)")
MACRO_CONFIG_INT(BcCustomAspectRatio, bc_custom_aspect_ratio, 0, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio value x100 (0=off, presets: 125=5:4, 133=4:3, 150=3:2, custom: 100-300)")

// Chat media
MACRO_CONFIG_INT(BcChatMediaPreview, bc_chat_media_preview, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render media previews from chat links")
MACRO_CONFIG_INT(BcChatMediaPhotos, bc_chat_media_photos, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render photo previews from chat links")
MACRO_CONFIG_INT(BcChatMediaGifs, bc_chat_media_gifs, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render GIF and animated media previews from chat links")
MACRO_CONFIG_INT(BcChatMediaPreviewMaxWidth, bc_chat_media_preview_max_width, 220, 120, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum width of chat media previews")
MACRO_CONFIG_INT(BcChatMediaViewer, bc_chat_media_viewer, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable fullscreen media viewer for chat previews")
MACRO_CONFIG_INT(BcChatMediaViewerMaxZoom, bc_chat_media_viewer_max_zoom, 800, 100, 2000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum zoom of the chat media viewer in percent")
