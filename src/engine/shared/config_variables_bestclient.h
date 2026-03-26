// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) ;
#endif

// Camera drift
MACRO_CONFIG_INT(BcCameraDrift, bc_camera_drift, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth camera drift that lags behind player movement")
MACRO_CONFIG_INT(BcCameraDriftAmount, bc_camera_drift_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of camera drift (1 = minimal drift, 200 = maximum drift)")
MACRO_CONFIG_INT(BcCameraDriftSmoothness, bc_camera_drift_smoothness, 20, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of camera drift (1 = near instant, 20 = very smooth)")
MACRO_CONFIG_INT(BcCameraDriftReverse, bc_camera_drift_reverse, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Reverse camera drift direction (camera drifts opposite to movement)")
MACRO_CONFIG_INT(BcDynamicFov, bc_dynamic_fov, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Increase FOV dynamically based on movement speed")
MACRO_CONFIG_INT(BcDynamicFovAmount, bc_dynamic_fov_amount, 50, 1, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Amount of dynamic FOV boost (1 = minimal boost, 200 = maximum boost)")
MACRO_CONFIG_INT(BcDynamicFovSmoothness, bc_dynamic_fov_smoothness, 20, 1, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Smoothness of dynamic FOV boost (1 = near instant, 20 = very smooth)")
MACRO_CONFIG_INT(BcCustomAspectRatioMode, bc_custom_aspect_ratio_mode, -1, -1, 2, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio mode (-1=legacy auto, 0=off, 1=preset, 2=custom)")
MACRO_CONFIG_INT(BcCustomAspectRatio, bc_custom_aspect_ratio, 0, 0, 300, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Aspect ratio value x100 (0=off, presets: 125=5:4, 133=4:3, 150=3:2, custom: 100-300)")
MACRO_CONFIG_INT(BcCrystalLaser, bc_crystal_laser, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render rifle and shotgun lasers with crystal shards and icy glow")
MACRO_CONFIG_INT(BcPrevMouseMaxDistance45Degrees, bc_prev_mouse_max_distance_45_degrees, 400, 0, 5000, CFGFLAG_CLIENT | CFGFLAG_SAVE | CFGFLAG_INSENSITIVE, "Previous maximum cursor distance for 45 degrees")
MACRO_CONFIG_INT(BcPrevInpMousesens45Degrees, bc_prev_inp_mousesens_45_degrees, 200, 1, 100000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous mouse sensitivity for 45 degrees")
MACRO_CONFIG_INT(BcToggle45Degrees, bc_toggle_45_degrees, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use toggle mode for 45 degrees bind")
MACRO_CONFIG_INT(BcPrevInpMousesensSmallSens, bc_prev_inp_mousesens_small_sens, 200, 1, 100000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Previous mouse sensitivity for small sens")
MACRO_CONFIG_INT(BcToggleSmallSens, bc_toggle_small_sens, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Use toggle mode for small sens bind")
MACRO_CONFIG_INT(BcGoresMode, bc_gores_mode, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Entity-like gores mode")
MACRO_CONFIG_INT(BcGoresModeDisableIfWeapons, bc_gores_mode_disable_weapons, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable gores mode when holding shotgun, grenade or laser")
MACRO_CONFIG_INT(BcCinematicCamera, bc_cinematic_camera, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable smooth cinematic camera movement in spectator freeview")
MACRO_CONFIG_INT(BcAnimations, bc_animations, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle BestClient UI animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimation, bc_module_ui_reveal_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle module settings reveal animations")
MACRO_CONFIG_INT(BcModuleUiRevealAnimationMs, bc_module_ui_reveal_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Module settings reveal time (in ms)")
MACRO_CONFIG_INT(BcIngameMenuAnimation, bc_ingame_menu_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle ingame ESC menu animation")
MACRO_CONFIG_INT(BcIngameMenuAnimationMs, bc_ingame_menu_animation_ms, 220, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Ingame ESC menu animation time (in ms)")
MACRO_CONFIG_INT(BcChatAnimation, bc_chat_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat animation")
MACRO_CONFIG_INT(BcChatAnimationMs, bc_chat_animation_ms, 300, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation time (in ms)")
MACRO_CONFIG_INT(BcChatOpenAnimation, bc_chat_open_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat open animation")
MACRO_CONFIG_INT(BcChatOpenAnimationMs, bc_chat_open_animation_ms, 220, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat open animation time (in ms)")
MACRO_CONFIG_INT(BcChatTypingAnimation, bc_chat_typing_animation, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle chat typing animation")
MACRO_CONFIG_INT(BcChatTypingAnimationMs, bc_chat_typing_animation_ms, 180, 1, 500, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat typing animation time (in ms)")
MACRO_CONFIG_INT(BcChatAnimationType, bc_chat_animation_type, 3, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Chat animation type")

// General visuals
MACRO_CONFIG_INT(BcSettingsLayout, bc_settings_layout, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Settings layout (0 = new, 1 = old)")
MACRO_CONFIG_INT(BcEmoticonShadow, bc_emoticon_shadow, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Draw shadow behind emoticons")
MACRO_CONFIG_INT(BcChatSaveDraft, bc_chat_save_draft, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep unfinished chat input when closing chat")

// Auto team lock
MACRO_CONFIG_INT(BcAutoTeamLock, bc_auto_team_lock, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Automatically lock your team after joining it")
MACRO_CONFIG_INT(BcAutoTeamLockDelay, bc_auto_team_lock_delay, 5, 0, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Delay before auto-locking team after joining, in seconds")

// Speedrun timer
MACRO_CONFIG_INT(BcSpeedrunTimer, bc_speedrun_timer, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer")
MACRO_CONFIG_INT(BcSpeedrunTimerTime, bc_speedrun_timer_time, 0, 0, 9999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer time (MMSS format)")
MACRO_CONFIG_INT(BcSpeedrunTimerHours, bc_speedrun_timer_hours, 0, 0, 99, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer hours")
MACRO_CONFIG_INT(BcSpeedrunTimerMinutes, bc_speedrun_timer_minutes, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer minutes")
MACRO_CONFIG_INT(BcSpeedrunTimerSeconds, bc_speedrun_timer_seconds, 0, 0, 59, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer seconds")
MACRO_CONFIG_INT(BcSpeedrunTimerMilliseconds, bc_speedrun_timer_milliseconds, 0, 0, 999, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Speedrun timer milliseconds")
MACRO_CONFIG_INT(BcSpeedrunTimerAutoDisable, bc_speedrun_timer_auto_disable, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Disable speedrun timer automatically when time ends")

// Music player
MACRO_CONFIG_INT(BcMusicPlayer, bc_music_player, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable Music Player HUD element")
MACRO_CONFIG_INT(BcMusicPlayerShowWhenPaused, bc_music_player_show_when_paused, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Keep Music Player visible while playback is paused")
MACRO_CONFIG_INT(BcMusicPlayerColorMode, bc_music_player_color_mode, 1, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Music player color mode (0=static color, 1=cover blur color)")
MACRO_CONFIG_COL(BcMusicPlayerStaticColor, bc_music_player_static_color, 128, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Static color for the music player when static color mode is selected")

// Cava / audio visualizer
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

// Media background
MACRO_CONFIG_INT(BcMenuMediaBackground, bc_menu_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in offline menus")
MACRO_CONFIG_INT(BcGameMediaBackground, bc_game_media_background, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Enable custom media background in game background rendering")
MACRO_CONFIG_STR(BcMenuMediaBackgroundPath, bc_menu_media_background_path, IO_MAX_PATH_LENGTH, "", CFGFLAG_CLIENT | CFGFLAG_SAVE, "Path to the custom menu media background file")
MACRO_CONFIG_INT(BcGameMediaBackgroundOffset, bc_game_media_background_offset, 0, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How much the custom media background is fixed to the map when rendering the in-game background")

// Afterimage
MACRO_CONFIG_INT(BcAfterimage, bc_afterimage, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Render afterimage layers of your tee")
MACRO_CONFIG_INT(BcAfterimageFrames, bc_afterimage_frames, 6, 2, 20, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How many previous frames to keep for the afterimage")
MACRO_CONFIG_INT(BcAfterimageAlpha, bc_afterimage_alpha, 40, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Maximum alpha of afterimage layers (1-100)")
MACRO_CONFIG_INT(BcAfterimageSpacing, bc_afterimage_spacing, 18, 1, 64, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Distance between afterimage samples")

// Chat bubbles
MACRO_CONFIG_INT(BcChatBubbles, bc_chat_bubbles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle Chatbubbles")
MACRO_CONFIG_INT(BcChatBubblesSelf, bc_chat_bubbles_self, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles above you")
MACRO_CONFIG_INT(BcChatBubblesDemo, bc_chat_bubbles_demo, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Show Chatbubbles in demoplayer")
MACRO_CONFIG_INT(BcChatBubbleSize, bc_chat_bubble_size, 20, 20, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of the chat bubble")
MACRO_CONFIG_INT(BcChatBubbleShowTime, bc_chat_bubble_showtime, 200, 200, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long to show the bubble for")
MACRO_CONFIG_INT(BcChatBubbleFadeOut, bc_chat_bubble_fadeout, 35, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades out")
MACRO_CONFIG_INT(BcChatBubbleFadeIn, bc_chat_bubble_fadein, 15, 15, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "How long it fades in")

// Magic particles
MACRO_CONFIG_INT(BcMagicParticles, bc_magic_particles, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle magic particles")
MACRO_CONFIG_INT(BcMagicParticlesRadius, bc_magic_particles_radius, 10, 1, 1000, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Radius of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesSize, bc_magic_particles_size, 8, 1, 50, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Size of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesAlphaDelay, bc_magic_particles_alpha_delay, 3, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Alpha delay of magic particles")
MACRO_CONFIG_INT(BcMagicParticlesType, bc_magic_particles_type, 1, 1, 4, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Type of magic particles. 1 = Slice, 2 = Ball, 3 = Smoke, 4 = Shell")
MACRO_CONFIG_INT(BcMagicParticlesCount, bc_magic_particles_count, 10, 1, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Count of magic particles")

// Orbit aura
MACRO_CONFIG_INT(BcOrbitAura, bc_orbit_aura, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Toggle orbit aura around the local player")
MACRO_CONFIG_INT(BcOrbitAuraRadius, bc_orbit_aura_radius, 32, 8, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura radius")
MACRO_CONFIG_INT(BcOrbitAuraParticles, bc_orbit_aura_particles, 14, 2, 120, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura particle count")
MACRO_CONFIG_INT(BcOrbitAuraAlpha, bc_orbit_aura_alpha, 70, 0, 100, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura alpha")
MACRO_CONFIG_INT(BcOrbitAuraSpeed, bc_orbit_aura_speed, 100, 10, 200, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Orbit aura speed")
MACRO_CONFIG_INT(BcOrbitAuraIdle, bc_orbit_aura_idle, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Only show orbit aura after standing idle")
MACRO_CONFIG_INT(BcOrbitAuraIdleTimer, bc_orbit_aura_idle_timer, 2, 1, 30, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Idle delay before orbit aura appears in seconds")

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

// Debug
MACRO_CONFIG_INT(DbgCava, dbg_cava, 0, 0, 2, CFGFLAG_CLIENT, "Debug Cava/Audio visualizer (1=overlay, 2=overlay+logs)")
