/* Copyright © 2026 BestProject Team */
// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Save, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Save, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Save, Desc) ;
#endif

// SettingsTabs
MACRO_CONFIG_INT(BcBestClientSettingsTabs, bc_bestclient_settings_tabs, 0, 0, 65536, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bit flags to disable BestClient settings tabs")

// Chat Filter
MACRO_CONFIG_INT(BcChatOnlyTagsAndWhispers, bc_chat_only_tags_and_whispers, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show only highlighted and private chat messages")
MACRO_CONFIG_INT(BcShowBlockedWordInConsole, bc_show_blocked_word_in_console, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show blocked word with regex in console")
MACRO_CONFIG_COL(BcBlockedWordConsoleColor, bc_blocked_word_console_color, 0x99ffff, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color of blocked word messages in console")
MACRO_CONFIG_INT(BcEnableCensorList, bc_enable_censor_list, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable chat filter")
MACRO_CONFIG_INT(BcMultipleReplacementChar, bc_multiple_replacement_char, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Multiple replacement char on blocked word len")
MACRO_CONFIG_STR(BcBlockedContentReplacementChar, bc_blocked_content_replacement_char, 64, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Character used to replace blocked content")
MACRO_CONFIG_STR(BcRegexPlayerWhitelist, bc_regex_player_whitelist, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat filter player whitelist")
MACRO_CONFIG_INT(BcFilterChangeWholeWord, bc_filter_change_whole_word, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Change whole word to replacement character")
MACRO_CONFIG_STR(BcBlockedContentPartialReplacementChar, bc_blocked_content_partial_replacement_char, 64, "*", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Character used to replace partial blocked content")

// Auto update
MACRO_CONFIG_INT(BcAutoUpdate, bc_auto_update, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically download and apply updates on startup")

// General visuals
MACRO_CONFIG_INT(BcSettingsLayout, bc_settings_layout, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings layout (0 = new, 1 = old)")
MACRO_CONFIG_INT(BcHideHudInSettings, bc_hide_hud_in_settings, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide HUD while settings page is open")
MACRO_CONFIG_INT(BcChatSaveDraft, bc_chat_save_draft, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep unfinished chat input when closing chat")
MACRO_CONFIG_INT(BcSilentTyping, bc_silent_typing, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide PLAYERFLAG_CHATTING from others while typing")
MACRO_CONFIG_INT(BcChatAltCommandLayout, bc_chat_alt_command_layout, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Interpret slash chat commands typed in the Russian keyboard layout")
MACRO_CONFIG_INT(BcCinematicCamera, bc_cinematic_camera, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth cinematic camera movement in spectator freeview")
MACRO_CONFIG_INT(BcCinematicCameraStrength, bc_cinematic_camera_strength, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of cinematic camera smoothing (0 = mild, 100 = strong)")
MACRO_CONFIG_INT(BcSpecMovedNotify, bc_spec_moved_notify, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show 'moved in game' warning when someone hooks/hits your character while you are in spectator")
MACRO_CONFIG_STR(BcSpecMovedNotifyText, bc_spec_moved_notify_text, 128, "moved in game", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Text shown when someone hooks/hits your character while you are in spectator")

// CameraDrift (demo playback only)
MACRO_CONFIG_INT(BcCameraDrift, bc_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable camera drift during demo playback")
MACRO_CONFIG_INT(BcCameraDriftAmount, bc_camera_drift_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of camera drift during demo playback")
MACRO_CONFIG_INT(BcCameraDriftSmoothness, bc_camera_drift_smoothness, 20, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of camera drift during demo playback")
MACRO_CONFIG_INT(BcCameraDriftReverse, bc_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse camera drift direction during demo playback")
// Freeview Numpad Camera (spectator/demo playback)
MACRO_CONFIG_INT(BcFreeviewNumpad, bc_freeview_numpad, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable numpad camera movement in freeview/spectator mode")
MACRO_CONFIG_INT(BcFreeviewSpeed, bc_freeview_speed, 180, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeview camera movement speed")
MACRO_CONFIG_INT(BcFreeviewSmoothness, bc_freeview_smoothness, 25, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Freeview camera smoothness (acceleration)")

// DynamicFov (demo playback only)
MACRO_CONFIG_INT(BcDynamicFov, bc_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable dynamic FOV during demo playback")
MACRO_CONFIG_INT(BcDynamicFovAmount, bc_dynamic_fov_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of dynamic FOV during demo playback")
MACRO_CONFIG_INT(BcDynamicFovSmoothness, bc_dynamic_fov_smoothness, 20, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of dynamic FOV during demo playback")

// Gradient nicknames / text / skin
MACRO_CONFIG_INT(BcNameplateGradient, bc_nameplate_gradient, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gradient player nicknames")
MACRO_CONFIG_INT(BcNameplateGradientClan, bc_nameplate_gradient_clan, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gradient player clan tags")
MACRO_CONFIG_INT(BcNameplateGradientMode, bc_nameplate_gradient_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Gradient mode (0 = skin colors, 1 = custom, 2 = rainbow)")
MACRO_CONFIG_INT(BcNameplateGradientColorCount, bc_nameplate_gradient_color_count, 2, 2, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Number of custom gradient colors")
MACRO_CONFIG_COL(BcNameplateGradientColor1, bc_nameplate_gradient_color1, 0xFF3366FFU, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Custom gradient color 1")
MACRO_CONFIG_COL(BcNameplateGradientColor2, bc_nameplate_gradient_color2, 0xFF66CCFFU, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Custom gradient color 2")
MACRO_CONFIG_COL(BcNameplateGradientColor3, bc_nameplate_gradient_color3, 0xFF33FF99U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Custom gradient color 3")
MACRO_CONFIG_COL(BcNameplateGradientColor4, bc_nameplate_gradient_color4, 0xFFFFCC33U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Custom gradient color 4")
MACRO_CONFIG_INT(BcNameplateGradientSkin, bc_nameplate_gradient_skin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply gradient colors to player skin body and feet")
MACRO_CONFIG_INT(BcNameplateGradientEverything, bc_nameplate_gradient_everything, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply gradient to all rendered text")
MACRO_CONFIG_INT(BcNameplateGradientAnimateSpeed, bc_nameplate_gradient_animate_speed, 35, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speed of the animated gradient sweep, in percent")
MACRO_CONFIG_INT(BcScoreboardTeamGradients, bc_scoreboard_team_gradients, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render scoreboard team colors with a left-to-right gradient")
MACRO_CONFIG_INT(BcShowPointsInTab, bc_show_points_in_tab, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player points in the scoreboard on DDNet and EGO servers")
MACRO_CONFIG_INT(BcShowhudDummyCoordIndicator, bc_showhud_dummy_coord_indicator, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show player-below indicator when aligned above another player")
MACRO_CONFIG_INT(BcShowCorrectCheckpoint, bc_show_correct_checkpoint, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show current tele checkpoint in movement information")

// SelfTimeCp
MACRO_CONFIG_INT(BcSelfTimeCp, bc_self_time_cp, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable personal time checkpoints")
MACRO_CONFIG_INT(BcSelfTimeCpPlaceMode, bc_self_time_cp_place_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Self timeCP placement mode (0 = tee, 1 = cursor)")
MACRO_CONFIG_COL(BcSelfTimeCpColor, bc_self_time_cp_color, 0x9933E6FFU, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of personal time checkpoint strips")

MACRO_CONFIG_INT(BcShowRealHitbox, bc_show_real_hitbox, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show a dot at the center of your real hitbox")
MACRO_CONFIG_COL(BcShowRealHitboxColor, bc_show_real_hitbox_color, 0xFFFFFFFFU, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of the real hitbox center dot")
MACRO_CONFIG_INT(BcMastersrv, bc_mastersrv, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the BestClient master server mirror for the server browser")

// Independent UI scale
MACRO_CONFIG_INT(BcScoreboardScale, bc_scoreboard_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Scoreboard scale in percent, independent of UI scale")
MACRO_CONFIG_INT(BcWheelScale, bc_wheel_scale, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Bind/emote/gif wheel scale in percent, independent of UI scale")

// Chat media
MACRO_CONFIG_INT(BcChatMediaPreview, bc_chat_media_preview, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render media previews from chat links")
MACRO_CONFIG_INT(BcChatMediaPhotos, bc_chat_media_photos, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render photo previews from chat links")
MACRO_CONFIG_INT(BcChatMediaGifs, bc_chat_media_gifs, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render GIF and animated media previews from chat links")
MACRO_CONFIG_INT(BcChatMediaContentFilter, bc_chat_media_content_filter, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow chat media previews only from configured domains")
MACRO_CONFIG_STR(BcChatMediaAllowedDomains, bc_chat_media_allowed_domains, 512, "tenor.com; imgur.com; giphy.com; gifs.teeworlds.xyz", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Semicolon-separated allowlist for chat media domains")
MACRO_CONFIG_INT(BcChatMediaPreviewMaxWidth, bc_chat_media_preview_max_width, 220, 120, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum width of chat media previews")

// CherryGifs provider
MACRO_CONFIG_INT(BcCherryGifsShowNsfw, bc_cherrygifs_show_nsfw, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Include NSFW gifs in the CherryGifs browser")
MACRO_CONFIG_INT(BcCherryGifsSortTop, bc_cherrygifs_sort_top, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Sort CherryGifs browser by top instead of new")

// Gif wheel bubble-above-head
MACRO_CONFIG_INT(BcGifBubbleAboveHead, bc_gif_bubble_above_head, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show a floating gif bubble above the sender when a recognized gif link is posted in chat")
MACRO_CONFIG_STR(BcGifBubbleDomains, bc_gif_bubble_domains, 256, "gifs.teeworlds.xyz", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Semicolon-separated domains that trigger the above-head gif bubble")
MACRO_CONFIG_INT(BcGifBubbleDurationMs, bc_gif_bubble_duration_ms, 5000, 1000, 15000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long the above-head gif bubble stays visible")
MACRO_CONFIG_INT(BcGifBubbleOffsetY, bc_gif_bubble_offset_y, 90, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset of the above-head gif bubble")

// Chat bubbles (above players)
MACRO_CONFIG_INT(BcChatBubbles, bc_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat bubbles above players")
MACRO_CONFIG_INT(BcChatBubblesSelf, bc_chat_bubbles_self, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat bubbles above yourself")
MACRO_CONFIG_INT(BcChatBubblesDemo, bc_chat_bubbles_demo, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show chat bubbles in demo playback")
MACRO_CONFIG_INT(BcChatBubbleSize, bc_chat_bubble_size, 20, 20, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of chat bubbles")
MACRO_CONFIG_INT(BcChatBubbleShowTime, bc_chat_bubble_showtime, 200, 200, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long to show chat bubbles (centiseconds)")
MACRO_CONFIG_INT(BcChatBubbleFadeOut, bc_chat_bubble_fadeout, 35, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble fade-out duration (centiseconds)")
MACRO_CONFIG_INT(BcChatBubbleFadeIn, bc_chat_bubble_fadein, 15, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble fade-in duration (centiseconds)")
MACRO_CONFIG_INT(BcChatBubbleAnimation, bc_chat_bubble_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Animate chat bubble stacking movement")
MACRO_CONFIG_INT(BcChatBubbleCustomColors, bc_chat_bubble_custom_colors, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use custom colors for chat bubbles")
MACRO_CONFIG_COL(BcChatBubbleBgColor, bc_chat_bubble_bg_color, 0x40000000U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat bubble background color")
MACRO_CONFIG_COL(BcChatBubbleTextColor, bc_chat_bubble_text_color, 0xFFFFFFFFU, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat bubble text color")
MACRO_CONFIG_COL(BcChatBubbleOutlineColor, bc_chat_bubble_outline_color, 0x80000000U, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Chat bubble text outline color")
MACRO_CONFIG_INT(BcChatBubbleRounding, bc_chat_bubble_rounding, 0, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat bubble corner rounding (0 = auto from size)")

// Animations
MACRO_CONFIG_INT(BcAnimations, bc_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle BestClient UI animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimation, bc_module_ui_reveal_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle module settings reveal animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimationMs, bc_module_ui_reveal_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Module settings reveal time (in ms)")
MACRO_CONFIG_INT(BcChatAnimation, bc_chat_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat animation")
MACRO_CONFIG_INT(BcChatAnimationMs, bc_chat_animation_ms, 300, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation time (in ms)")
MACRO_CONFIG_INT(BcChatOpenAnimation, bc_chat_open_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat open animation")
MACRO_CONFIG_INT(BcChatOpenAnimationMs, bc_chat_open_animation_ms, 220, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat open animation time (in ms)")
MACRO_CONFIG_INT(BcChatTypingAnimation, bc_chat_typing_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat typing animation")
MACRO_CONFIG_INT(BcChatTypingAnimationMs, bc_chat_typing_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat typing animation time (in ms)")
MACRO_CONFIG_INT(BcKillfeedAnimation, bc_killfeed_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle killfeed animation")
MACRO_CONFIG_INT(BcKillfeedAnimationMs, bc_killfeed_animation_ms, 200, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Killfeed animation time (in ms)")
MACRO_CONFIG_INT(BcMainMenuAnimation, bc_main_menu_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle main menu button hover animation")
MACRO_CONFIG_INT(BcMainMenuAnimationSpeed, bc_main_menu_animation_speed, 12, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Main menu hover animation speed")
MACRO_CONFIG_INT(BcChatAnimationType, bc_chat_animation_type, 3, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation type")

// Auto team lock
MACRO_CONFIG_INT(BcAutoTeamLock, bc_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically lock your team after joining it")
MACRO_CONFIG_INT(BcAutoTeamLockDelay, bc_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay before auto-locking team after joining, in seconds")

// Extend zoom
MACRO_CONFIG_INT(BcExtendZoom, bc_extend_zoom, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use 0.5 zoom steps instead of 1 for finer scroll zoom (10, 9.5, 9, 8.5 ...)")

// MuteOthers
MACRO_CONFIG_INT(BcMuteOthersHook, bc_mute_others_hook, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute hook sounds of other players")
MACRO_CONFIG_INT(BcMuteOthersHammer, bc_mute_others_hammer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute hammer sounds of other players")

// Translate
MACRO_CONFIG_STR(BcTranslateIncomingSource, bc_translate_incoming_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language for incoming chat translation (use auto to detect)")
MACRO_CONFIG_STR(BcTranslateIncomingIgnoreLanguages, bc_translate_incoming_ignore_languages, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Languages that should not be translated (incoming or outgoing), e.g. es/ru/zh")
MACRO_CONFIG_STR(BcTranslateOutgoingSource, bc_translate_outgoing_source, 16, "auto", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Source language for your outgoing chat translation (use auto to detect)")
MACRO_CONFIG_STR(BcTranslateOutgoingTarget, bc_translate_outgoing_target, 16, "en", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Target language for your outgoing chat translation")

// Casino
MACRO_CONFIG_INT(BcCasinoBalance, bc_casino_balance, 500, 0, 9999999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Casino game balance in dollars")
MACRO_CONFIG_INT(BcCasinoLastClaim, bc_casino_last_claim, 0, 0, 2147483647, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Unix timestamp of last casino free claim")

// Eye comfort
MACRO_CONFIG_INT(BcEyeComfort, bc_eye_comfort, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render a warm yellow screen overlay to reduce eye strain")
MACRO_CONFIG_INT(BcEyeComfortStrength, bc_eye_comfort_strength, 35, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Eye comfort intensity in percent; increases warmth and lowers brightness together")

// HookCombo
MACRO_CONFIG_INT(BcHookCombo, bc_hook_combo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show hook combo popups with combo sounds")
MACRO_CONFIG_INT(BcHookComboMode, bc_hook_combo_mode, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo trigger mode (0=hook, 1=hammer, 2=hook and hammer)")
MACRO_CONFIG_INT(BcHookComboResetTime, bc_hook_combo_reset_time, 1200, 100, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum time in ms between player hooks before combo restarts")
MACRO_CONFIG_INT(BcHookComboSoundVolume, bc_hook_combo_sound_volume, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo sound volume")
MACRO_CONFIG_INT(BcHookComboSize, bc_hook_combo_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hook combo popup size")

// Swap timer
MACRO_CONFIG_INT(BcSwapTimer, bc_swap_timer, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show a HUD timer for pending swap requests")
MACRO_CONFIG_INT(BcSwapTimerStyle, bc_swap_timer_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Swap timer style (0 = HUD, 1 = Nameplate)")
MACRO_CONFIG_STR(BcSwapTimerWaitText, bc_swap_timer_wait_text, 32, "[%ds]", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimal nameplate cooldown text (%d seconds, %y your name, %n other name)")
MACRO_CONFIG_STR(BcSwapTimerLeftText, bc_swap_timer_left_text, 32, "[%ds]", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minimal nameplate remaining time text (%d seconds, %y your name, %n other name)")

MACRO_CONFIG_INT(BcSwapTimerSize, bc_swap_timer_size, 100, 50, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Swap timer size")
MACRO_CONFIG_INT(BcSwapTimerShowHotkeys, bc_swap_timer_show_hotkeys, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show accept and decline hotkeys in the swap timer")
MACRO_CONFIG_INT(BcSwapTimerShowTees, bc_swap_timer_show_tees, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show tee icons next to the names in the swap timer")

// HUD editor
MACRO_CONFIG_INT(BcHudMusicPlayerX, bc_hud_music_player_x, 198, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor X position for music player")
MACRO_CONFIG_INT(BcHudMusicPlayerY, bc_hud_music_player_y, 0, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor Y position for music player")
MACRO_CONFIG_INT(BcHudMusicPlayerScale, bc_hud_music_player_scale, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor scale for music player")
MACRO_CONFIG_INT(BcHudVoiceHudX, bc_hud_voice_hud_x, 0, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor X position for voice HUD")
MACRO_CONFIG_INT(BcHudVoiceHudY, bc_hud_voice_hud_y, 100, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor Y position for voice HUD")
MACRO_CONFIG_INT(BcHudVoiceHudScale, bc_hud_voice_hud_scale, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor scale for voice HUD")
MACRO_CONFIG_INT(BcHudVoiceMuteIconsX, bc_hud_voice_mute_icons_x, 136, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor X position for voice mute icons")
MACRO_CONFIG_INT(BcHudVoiceMuteIconsY, bc_hud_voice_mute_icons_y, 0, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor Y position for voice mute icons")
MACRO_CONFIG_INT(BcHudVoiceMuteIconsScale, bc_hud_voice_mute_icons_scale, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor scale for voice mute icons")
MACRO_CONFIG_INT(BcHudChatX, bc_hud_chat_x, 5, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor X position for ingame chat")
MACRO_CONFIG_INT(BcHudChatY, bc_hud_chat_y, 278, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor Y anchor position for ingame chat")
MACRO_CONFIG_INT(BcHudChatScale, bc_hud_chat_scale, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor scale for ingame chat")
MACRO_CONFIG_INT(BcHudVotesX, bc_hud_votes_x, 0, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor X position for votes")
MACRO_CONFIG_INT(BcHudVotesY, bc_hud_votes_y, 60, -1000, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor Y position for votes")
MACRO_CONFIG_INT(BcHudVotesScale, bc_hud_votes_scale, 100, 25, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "HUD editor scale for votes")

// Voice chat
MACRO_CONFIG_INT(BcVoiceChatEnable, bc_voice_chat_enable, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable integrated voice chat")
MACRO_CONFIG_INT(BcVoiceChatActivationMode, bc_voice_chat_activation_mode, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice activation mode (0 = automatic activation, 1 = push-to-talk)")
MACRO_CONFIG_INT(BcVoiceChatVadThreshold, bc_voice_chat_vad_threshold, 5, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice activation threshold in automatic mode (percent)")
MACRO_CONFIG_INT(BcVoiceChatVadReleaseDelayMs, bc_voice_chat_vad_release_delay_ms, 150, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay before automatic voice activation turns off (ms)")
MACRO_CONFIG_INT(BcVoiceChatVolume, bc_voice_chat_volume, 100, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice playback volume in percent")
MACRO_CONFIG_INT(BcVoiceChatMicGain, bc_voice_chat_mic_gain, 100, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Microphone gain in percent")
MACRO_CONFIG_INT(BcVoiceChatBitrate, bc_voice_chat_bitrate, 96, 6, 128, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice Opus bitrate in kbps")
MACRO_CONFIG_INT(BcVoiceChatInputDevice, bc_voice_chat_input_device, -1, -1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice input device index (-1 = system default)")
MACRO_CONFIG_INT(BcVoiceChatOutputDevice, bc_voice_chat_output_device, -1, -1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice output device index (-1 = system default)")
MACRO_CONFIG_INT(BcVoiceChatMicMuted, bc_voice_chat_mic_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute voice microphone")
MACRO_CONFIG_INT(BcVoiceChatHeadphonesMuted, bc_voice_chat_headphones_muted, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mute voice playback")
MACRO_CONFIG_INT(BcVoiceChatMicCheck, bc_voice_chat_mic_check, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable microphone check (local loopback)")
MACRO_CONFIG_INT(BcVoiceChatInGameOnly, bc_voice_chat_ingame_only, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only allow voice transmit/playback while the game window is active")
MACRO_CONFIG_INT(BcVoiceChatUseTeam0, bc_voice_chat_use_team0, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Force voice chat to use team 0 even while you are in a team")
MACRO_CONFIG_INT(BcVoiceChatEnableYourGroup, bc_voice_chat_enable_your_group, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "In team0 mode, also include your own team for voice transmit/playback")
MACRO_CONFIG_INT(BcVoiceChatRadiusEnabled, bc_voice_chat_radius_enabled, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable local voice radius filter")
MACRO_CONFIG_INT(BcVoiceChatRadiusTiles, bc_voice_chat_radius_tiles, 25, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice radius in tiles when local radius filter is enabled")
MACRO_CONFIG_INT(BcVoiceChatNameplateIcon, bc_voice_chat_nameplate_icon, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show microphone icon in name plates for talking players")
MACRO_CONFIG_STR(BcVoiceChatServerAddress, bc_voice_chat_server_address, 128, "managed", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice server address")
MACRO_CONFIG_STR(BcVoiceChatMutedNames, bc_voice_chat_muted_names, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Comma-separated list of muted voice nicknames (case-insensitive)")
MACRO_CONFIG_STR(BcVoiceChatNameVolumes, bc_voice_chat_name_volumes, 512, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Comma-separated list of voice nickname volumes in percent (name=value, case-insensitive)")
MACRO_CONFIG_STR(BcVoiceModKey, bc_voice_mod_key, 128, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Voice moderation key (saved for convenience)")

// Jelly tee
MACRO_CONFIG_INT(BcJellyTee, bc_jelly_tee, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable jelly tee deformation")
MACRO_CONFIG_INT(BcJellyTeeOthers, bc_jelly_tee_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply jelly tee deformation to other players")
MACRO_CONFIG_INT(BcJellyTeeStrength, bc_jelly_tee_strength, 500, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of jelly tee deformation")
MACRO_CONFIG_INT(BcJellyTeeDuration, bc_jelly_tee_duration, 30, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Duration of jelly tee deformation")

// 3D particles
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
MACRO_CONFIG_INT(Bc3dParticlesColorMode, bc_3d_particles_color_mode, 1, 1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Color mode for 3D particles. 1 = Custom, 2 = Random")
MACRO_CONFIG_COL(Bc3dParticlesColor, bc_3d_particles_color, 4294967295, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_COLALPHA, "Color of 3D particles")
MACRO_CONFIG_INT(Bc3dParticlesGlow, bc_3d_particles_glow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable 3D particles glow")
MACRO_CONFIG_INT(Bc3dParticlesGlowAlpha, bc_3d_particles_glow_alpha, 35, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow alpha of 3D particles (1-100)")
MACRO_CONFIG_INT(Bc3dParticlesGlowOffset, bc_3d_particles_glow_offset, 2, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Glow offset for 3D particles")

// Sweat weapon
MACRO_CONFIG_INT(BcCrystalLaser, bc_crystal_laser, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render rifle and shotgun lasers with crystal shards and icy glow")

// Flying name plates
MACRO_CONFIG_INT(BcFlyingNamePlates, bc_flying_name_plates, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render name plates like flying kites attached to players")
MACRO_CONFIG_INT(BcFlyingNamePlatesHideLine, bc_flying_name_plates_hide_line, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide the line between players and flying name plates")
MACRO_CONFIG_INT(BcFlyingNamePlatesLift, bc_flying_name_plates_lift, 28, 0, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Extra vertical lift for flying name plates")
MACRO_CONFIG_INT(BcFlyingNamePlatesDrag, bc_flying_name_plates_drag, 52, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How far flying name plates trail behind movement")
MACRO_CONFIG_INT(BcFlyingNamePlatesFollow, bc_flying_name_plates_follow, 40, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How quickly flying name plates catch up to their target position")

// Motion blur
MACRO_CONFIG_INT(BcMotionBlur, bc_motion_blur, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable motion blur via previous-frame blending")
MACRO_CONFIG_INT(BcMotionBlurStrength, bc_motion_blur_strength, 50, 0, 95, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Strength of motion blur frame blending (0-95%)")

// Music player
MACRO_CONFIG_INT(BcMusicPlayer, bc_music_player, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Music Player HUD element")
MACRO_CONFIG_INT(BcMusicPlayerShowWhenPaused, bc_music_player_show_when_paused, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep Music Player visible while playback is paused")
MACRO_CONFIG_INT(BcMusicPlayerVisualizerMode, bc_music_player_visualizer_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player visualizer mode (0=bottom, 1=center, 2=up)")
MACRO_CONFIG_INT(BcMusicPlayerVisualizerSmoothing, bc_music_player_visualizer_smoothing, 50, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player visualizer smoothing in percent")
MACRO_CONFIG_INT(BcMusicPlayerVisualizerRounding, bc_music_player_visualizer_rounding, 0, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player visualizer rounding (0=cube, 200=soft)")
MACRO_CONFIG_INT(BcMusicPlayerVisualizerColumns, bc_music_player_visualizer_columns, 5, 5, 10, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player visualizer bar count")
MACRO_CONFIG_INT(BcMusicPlayerColorMode, bc_music_player_color_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player color mode (0=static visualizer, 1=cover visualizer, 2=translucent)")
MACRO_CONFIG_COL(BcMusicPlayerStaticColor, bc_music_player_static_color, 128, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Static color for the music player when static color mode is selected")
MACRO_CONFIG_INT(BcMusicPlayerTextScale, bc_music_player_text_scale, 110, 70, 150, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player text scale in percent")
MACRO_CONFIG_INT(BcMusicPlayerShowLyrics, bc_music_player_show_lyrics, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show synchronized song lyrics in the music player (BETA)")
MACRO_CONFIG_INT(BcMusicPlayerShowCurrentTime, bc_music_player_show_current_time, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show current time tab under lyrics in the music player")
MACRO_CONFIG_INT(BcMusicPlayerUseColorForHud, bc_music_player_use_color_for_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use the Music Player color for HUD rectangles in hud.cpp")
MACRO_CONFIG_INT(BcMusicPlayerHudColorAlpha, bc_music_player_hud_color_alpha, 100, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha multiplier for the Music Player and HUD colors")
MACRO_CONFIG_INT(DbgMusicPlayer, dbg_music_player, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Debug logging for music player (0=off, 1=state changes, 2=verbose periodic diagnostics)")

// Aspect ratio
MACRO_CONFIG_INT(BcCustomAspectRatioMode, bc_custom_aspect_ratio_mode, -1, -1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio mode (-1=legacy auto, 0=off, 1=preset, 2=custom)")
MACRO_CONFIG_INT(BcCustomAspectRatioApplyMode, bc_custom_aspect_ratio_apply_mode, 1, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio apply mode (0=game only, 1=full, 2=game no hud)")
MACRO_CONFIG_INT(BcCustomAspectRatio, bc_custom_aspect_ratio, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio value x100 (0=off, presets: 125=5:4, 133=4:3, 150=3:2, custom: 100-1000)")
MACRO_CONFIG_INT(BcCustomAspectRatioNum, bc_custom_aspect_ratio_num, 0, 0, 100000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom aspect ratio numerator (width), 0=unset")
MACRO_CONFIG_INT(BcCustomAspectRatioDen, bc_custom_aspect_ratio_den, 0, 0, 100000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Custom aspect ratio denominator (height), 0=unset")

// Gores mode
MACRO_CONFIG_INT(BcGoresMode, bc_gores_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Entity-like gores mode")
MACRO_CONFIG_INT(BcGoresModeDisableIfWeapons, bc_gores_mode_disable_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable gores mode when holding shotgun, grenade or laser")

// Speedrun timer
MACRO_CONFIG_INT(BcSpeedrunTimer, bc_speedrun_timer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer")
MACRO_CONFIG_INT(BcSpeedrunTimerHours, bc_speedrun_timer_hours, 0, 0, 99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer hours")
MACRO_CONFIG_INT(BcSpeedrunTimerMinutes, bc_speedrun_timer_minutes, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer minutes")
MACRO_CONFIG_INT(BcSpeedrunTimerSeconds, bc_speedrun_timer_seconds, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer seconds")
MACRO_CONFIG_INT(BcSpeedrunTimerMilliseconds, bc_speedrun_timer_milliseconds, 0, 0, 999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer milliseconds")
MACRO_CONFIG_INT(BcSpeedrunTimerAutoDisable, bc_speedrun_timer_auto_disable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable speedrun timer automatically when time ends")

// Keystrokes
MACRO_CONFIG_INT(BcKeystrokesStyle, bc_keystrokes_style, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keystrokes style (0 = Classic, 1 = Minecraft)")
MACRO_CONFIG_INT(BcKeystrokesKeyboard, bc_keystrokes_keyboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show keyboard keystrokes HUD")
MACRO_CONFIG_INT(BcKeystrokesKeyboardPreset, bc_keystrokes_keyboard_preset, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keyboard keystrokes preset (0 = wasd-minimal, 1 = wasd-full, 2 = micro)")
MACRO_CONFIG_INT(BcKeystrokesMouse, bc_keystrokes_mouse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show mouse keystrokes HUD")
MACRO_CONFIG_INT(BcKeystrokesMousePreset, bc_keystrokes_mouse_preset, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Mouse keystrokes preset (1 = mouse-arrow, 2 = mouse-dot-dot, 3 = mouse-nothing)")
MACRO_CONFIG_INT(BcKeystrokesMcLayout, bc_keystrokes_mc_layout, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Minecraft keystrokes layout (0 = Full, 1 = Only A/D)")
MACRO_CONFIG_INT(BcKeystrokesMcShowWs, bc_keystrokes_mc_show_ws, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show W/S in Minecraft Full layout")
MACRO_CONFIG_INT(BcKeystrokesMcShowLmb, bc_keystrokes_mc_show_lmb, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show LMB in Minecraft keystrokes")
MACRO_CONFIG_INT(BcKeystrokesMcShowRmb, bc_keystrokes_mc_show_rmb, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show RMB in Minecraft keystrokes")
MACRO_CONFIG_INT(BcKeystrokesMcShowSpace, bc_keystrokes_mc_show_space, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Space in Minecraft keystrokes")

// Finish prediction
MACRO_CONFIG_INT(BcFinishPrediction, bc_finish_prediction, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable finish prediction")
MACRO_CONFIG_INT(BcFinishPredictionShowAlways, bc_finish_prediction_show_always, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show finish prediction even before race start")
MACRO_CONFIG_INT(BcFinishPredictionTimeMode, bc_finish_prediction_time_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Finish prediction time mode (0=remaining, 1=predicted finish time)")
MACRO_CONFIG_INT(BcFinishPredictionShowTime, bc_finish_prediction_show_time, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show finish prediction time")
MACRO_CONFIG_INT(BcFinishPredictionShowPercentage, bc_finish_prediction_show_percentage, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show finish prediction progress percentage")
MACRO_CONFIG_INT(BcFinishPredictionShowMillis, bc_finish_prediction_show_millis, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show finish prediction milliseconds")
MACRO_CONFIG_INT(BcFinishPredictionAnalyseTeleFreeze, bc_finish_prediction_analyse_tele_freeze, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Analyse teleports and pass-through freeze walls for finish prediction")

// Fast Actions
MACRO_CONFIG_INT(BcFastActions, bc_fast_actions, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable the Fast Actions quick command selector")

// Quick binds (45 degrees / small sens)
MACRO_CONFIG_INT(BcPrevMouseMaxDistance45Degrees, bc_prev_mouse_max_distance_45_degrees, 400, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Previous maximum cursor distance for 45 degrees")
MACRO_CONFIG_INT(BcPrevInpMousesens45Degrees, bc_prev_inp_mousesens_45_degrees, 200, 1, 1000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous mouse sensitivity for 45 degrees")
MACRO_CONFIG_INT(BcToggle45Degrees, bc_toggle_45_degrees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use toggle mode for 45 degrees bind")
MACRO_CONFIG_INT(BcPrevInpMousesensSmallSens, bc_prev_inp_mousesens_small_sens, 200, 1, 1000000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous mouse sensitivity for small sens")
MACRO_CONFIG_INT(BcToggleSmallSens, bc_toggle_small_sens, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use toggle mode for small sens bind")

// Inputs
MACRO_CONFIG_INT(BcInputs, bc_inputs, 0, 0, 6, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Input prediction mode (0 = off, 1 = fast, 2 = best, 3 = saiko, 4 = delta, 5 = f, 6 = cloud)")
MACRO_CONFIG_INT(BcBestInputAmount, bc_best_input_amount, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input prediction amount in 0.01 ticks")
MACRO_CONFIG_INT(BcBestInputSmoothing, bc_best_input_smoothing, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input smoothing amount (0-100%)")
MACRO_CONFIG_INT(BcBestInputLatencyComp, bc_best_input_latency_comp, 0, 0, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input latency compensation (0-50%)")
MACRO_CONFIG_INT(BcBestInputInterpolation, bc_best_input_interpolation, 1, 1, 3, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Best input interpolation mode (1 = linear, 2 = cubic, 3 = smooth)")
MACRO_CONFIG_INT(BcBestInputOthers, bc_best_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply best input to other tees")
MACRO_CONFIG_INT(BcSaikoInputAmount, bc_saiko_input_amount, 0, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Saiko input amount in 0.01 ticks")
MACRO_CONFIG_INT(BcSaikoInputOthers, bc_saiko_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply Saiko input to other tees")
MACRO_CONFIG_INT(BcDeltaInputAmount, bc_delta_input_amount, 0, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delta input amount in 0.01 ticks")
MACRO_CONFIG_INT(BcDeltaInputOthers, bc_delta_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply delta input to other tees")
MACRO_CONFIG_INT(BcFInputAmount, bc_f_input_amount, 0, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "F input amount in 0.001-tick units (0=off, 1000=1.000 ticks, max 5000=5.000 ticks)")
MACRO_CONFIG_INT(BcFInputOthers, bc_f_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply F input to other tees")
MACRO_CONFIG_INT(BcCloudInputAmount, bc_cloud_input_amount, 0, 0, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cloud input amount in 0.01 ticks")
MACRO_CONFIG_INT(BcCloudInputOthers, bc_cloud_input_others, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Apply cloud input to other tees")
MACRO_CONFIG_INT(BcAutoMargin, bc_auto_margin, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto margin")
MACRO_CONFIG_INT(BcSnapTap, bc_snap_tap, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Snap Tap for opposite left/right inputs")
MACRO_CONFIG_INT(BcSnapTapDelay, bc_snap_tap_delay, 0, 0, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Snap Tap direction switch delay in milliseconds (0 = off)")

// Browser Utils
MACRO_CONFIG_INT(BcAutoServerListRefresh, bc_auto_server_list_refresh, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically refresh the server browser list while a browser tab is open")
MACRO_CONFIG_INT(BcAutoServerListRefreshSeconds, bc_auto_server_list_refresh_seconds, 10, 1, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Auto refresh interval for the server browser list in seconds")
MACRO_CONFIG_INT(BcUseShortKogServerName, bc_use_short_kog_server_name, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use short name for KoG gores servers in browser list")

// Media background
MACRO_CONFIG_INT(BcMenuMediaBackground, bc_menu_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in offline menus")
MACRO_CONFIG_INT(BcGameMediaBackground, bc_game_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in game background rendering")
MACRO_CONFIG_STR(BcMenuMediaBackgroundPath, bc_menu_media_background_path, IO_MAX_PATH_LENGTH, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Path to the custom menu media background file")
MACRO_CONFIG_INT(BcGameMediaBackgroundOffset, bc_game_media_background_offset, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the custom media background is fixed to the map when rendering the in-game background")

// Optimizer
MACRO_CONFIG_INT(BcOptimizer, bc_optimizer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable optimizer features")
MACRO_CONFIG_INT(BcOptimizerDisableParticles, bc_optimizer_disable_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable rendering/updating all particles")
MACRO_CONFIG_INT(BcOptimizerFpsFog, bc_optimizer_fps_fog, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cull non-map rendering outside a distance limit around the camera")
MACRO_CONFIG_INT(BcOptimizerFpsFogMode, bc_optimizer_fps_fog_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog mode (0=manual radius tiles, 1=by zoom percent)")
MACRO_CONFIG_INT(BcOptimizerFpsFogRadiusTiles, bc_optimizer_fps_fog_radius_tiles, 40, 5, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog manual radius in tiles (tile=32 units)")
MACRO_CONFIG_INT(BcOptimizerFpsFogZoomPercent, bc_optimizer_fps_fog_zoom_percent, 70, 10, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "FPS fog visible area percent in zoom mode")
MACRO_CONFIG_INT(BcOptimizerFpsFogRenderRect, bc_optimizer_fps_fog_render_rect, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render an outline rectangle showing the FPS fog area")
MACRO_CONFIG_INT(BcOptimizerFpsFogCullMapTiles, bc_optimizer_fps_fog_cull_map_tiles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Cull map tile rendering outside the FPS fog area")

// Process priority (from Entity-Client)
MACRO_CONFIG_INT(BcHighProcessPriority, bc_high_process_priority, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Set DDNets process priority to high")
MACRO_CONFIG_INT(BcDiscordNormalProcessPriority, bc_discord_normal_process_priority, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Set Discord process priority to normal")

// Focus Mode Settings
MACRO_CONFIG_INT(ClFocusMode, p_focus_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable focus mode to minimize visual distractions")
MACRO_CONFIG_INT(ClFocusModeHideNames, p_focus_mode_hide_names, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide player names in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideEffects, p_focus_mode_hide_effects, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide visual effects in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideHud, p_focus_mode_hide_hud, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide HUD in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideSongPlayer, p_focus_mode_hide_song_player, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide song player in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideUI, p_focus_mode_hide_ui, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide unnecessary UI elements in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideChat, p_focus_mode_hide_chat, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide chat in focus mode")
MACRO_CONFIG_INT(ClFocusModeHideScoreboard, p_focus_mode_hide_scoreboard, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Hide scoreboard in focus mode")

// Client Indicator
MACRO_CONFIG_INT(BcClientIndicator, bc_client_indicator, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator is always enabled")
MACRO_CONFIG_INT(BcClientIndicatorVersions, bc_client_indicator_versions, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show versions tab in BestClient server browser panel")
MACRO_CONFIG_INT(DbgClientIndicator, dbg_client_indicator, 0, 0, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Debug logging for BestClient indicator (1=verbose, 2=dump all UDP packet bytes sent/received)")
MACRO_CONFIG_INT(BcNameplateClientIndicatorOffsetX, bc_nameplate_client_indicator_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the client indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateClientIndicatorOffsetY, bc_nameplate_client_indicator_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the client indicator in nameplates")
MACRO_CONFIG_INT(BcNameplateVoiceOffsetX, bc_nameplate_voice_offset_x, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Horizontal offset for the voice icon in nameplates")
MACRO_CONFIG_INT(BcNameplateVoiceOffsetY, bc_nameplate_voice_offset_y, 0, -400, 400, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Vertical offset for the voice icon in nameplates")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlate, bc_client_indicator_in_name_plate, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator in name plate")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateAboveSelf, bc_client_indicator_in_name_plate_above_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator above self")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateSize, bc_client_indicator_in_name_plate_size, 30, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Client indicator in name plate size")
MACRO_CONFIG_INT(BcClientIndicatorInNamePlateDynamic, bc_client_indicator_in_name_plate_dynamic, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator in nameplates will dynamically change pos")
MACRO_CONFIG_INT(IndicatorVersion, indicator_version, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show BestClient version in name plates")
MACRO_CONFIG_INT(BcClientIndicatorInScoreboard, bc_client_indicator_in_scoreboard, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show client indicator in name plate")
MACRO_CONFIG_INT(BcClientIndicatorInSoreboardSize, bc_client_indicator_in_scoreboard_size, 100, -50, 100, CFGFLAG_SAVE | CFGFLAG_CLIENT, "Client indicator in name plate size")
MACRO_CONFIG_STR(BcClientIndicatorServerAddress, bc_client_indicator_server_address, 256, "150.241.70.188:8778", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator UDP presence server")
MACRO_CONFIG_STR(BcClientIndicatorBrowserUrl, bc_client_indicator_browser_url, 256, "https://150.241.70.188:8779/users.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator browser JSON URL")
MACRO_CONFIG_STR(BcClientIndicatorTokenUrl, bc_client_indicator_token_url, 256, "https://150.241.70.188:8779/token.json", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator token bootstrap URL")
MACRO_CONFIG_STR(BcClientIndicatorSharedToken, bc_client_indicator_shared_token, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator shared token for signed UDP packets")
MACRO_CONFIG_STR(BcClientIndicatorSecretKey, bc_client_indicator_secret_key, 256, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Client indicator developer secret key")
MACRO_CONFIG_INT(BrFilterBestclient, br_filter_bestclient, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Filter out servers with no BestClient users")

// Twitch chat integration
MACRO_CONFIG_STR(BcTwitchChatNick, bc_twitch_chat_nick, 64, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Twitch channel nick for chat integration")

// Physic Balls (from Entity-Client)
MACRO_CONFIG_STR(BcPhysicBallsSkin, bc_physic_balls_skin, 24, "volleyball", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Base skin for physic balls")

// Better Spectate / Spec Pause Radio (from Entity-Client)
MACRO_CONFIG_INT(BcBetterSpectate, bc_better_spectate, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Replace say /pause bind (default Q) with +specpause radio menu")
MACRO_CONFIG_INT(BcSpecPauseShowDelay, bc_spec_pause_show_delay, 0, 0, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay before the spec pause radio becomes visible (ms)")

// Clans
MACRO_CONFIG_INT(BcClansEnabled, bc_clans_enabled, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Clans menu page")
MACRO_CONFIG_STR(BcClansApiUrl, bc_clans_api_url, 128, "https://clans.bestclient.fun", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Clans API base URL (allowlisted)")
MACRO_CONFIG_INT(BcClansAllowLocalDev, bc_clans_allow_local_dev, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Allow localhost clans API for development")
MACRO_CONFIG_INT(BcClansUnreadBadge, bc_clans_unread_badge, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show unread badge on Clans menubar button")
