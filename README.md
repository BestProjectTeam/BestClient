<div align="center">
  <img src="https://badges4noxy.vercel.app/badges/bestclient/bc_logo.webp" alt="BestClient" width="640" />
  <p>Custom DDNet client fork focused on comfort, customization, native tooling, and competitive quality-of-life features.</p>

  <p>
    <a href="https://bestclient.fun"><img src="https://badges4noxy.vercel.app/badges/bestclient/website.svg" alt="Website"/></a>
    <a href="https://t.me/bestddnet"><img src="https://badges4noxy.vercel.app/badges/bestclient/telegram.svg" alt="Telegram"/></a>
    <a href="https://discord.gg/bestclient"><img src="https://badges4noxy.vercel.app/badges/bestclient/discord.svg" alt="Discord"/></a>
  </p>
</div>

## About

BestClient is a customized DDNet client built around native BestClient systems instead of a thin reskin.
The project adds its own rendering stack, input tools, social systems, editors, and UI modules while staying compatible with DDNet servers.

The client currently ships with **278** `bc_*` config variables, five BestClient settings tabs, integrated voice chat, a live HUD editor, clans, collaborative multimapping, Fast Practice, and a Fun tab with mini-games.

## Links

- Website: [bestclient.fun](https://bestclient.fun)
- Telegram: [t.me/bestddnet](https://t.me/bestddnet)
- Discord: [discord.gg/bestclient](https://discord.gg/bestclient)

## Screenshots

### Main menu

<p align="center">
  <img src="docs/screenshots/mainmenu.jpg" width="70%" alt="Main menu" />
</p>

### BestClient settings tabs

#### Visuals

<p align="center">
  <img src="docs/screenshots/visuals_1.jpg" width="32%" alt="Visuals 1" />
  <img src="docs/screenshots/visuals_2.jpg" width="32%" alt="Visuals 2" />
  <img src="docs/screenshots/visuals_3.jpg" width="32%" alt="Visuals 3" />
</p>

#### Gameplay · Others

<p align="center">
  <img src="docs/screenshots/gameplay.jpg" width="32%" alt="Gameplay" />
  <img src="docs/screenshots/others_1.jpg" width="32%" alt="Others 1" />
  <img src="docs/screenshots/others_2.jpg" width="32%" alt="Others 2" />
</p>

#### Fun · Info

<p align="center">
  <img src="docs/screenshots/fun.jpg" width="49%" alt="Fun" />
  <img src="docs/screenshots/info.jpg" width="49%" alt="Info" />
</p>

## Highlight features

<table>
<tr>
<td width="50%" valign="top" align="left">
<strong>Clans</strong><br><br>
<p align="center"><img src="docs/screenshots/clans.jpg" width="100%" alt="Clans" /></p>
<ul>
<li>Register/login, create or join clans, roles, applications and announcements</li>
<li>Online member status and in-game clan tag sync</li>
<li>Unread badge on the menu button</li>
</ul>
</td>
<td width="50%" valign="top" align="left">
<strong>Multimapping</strong><br><br>
<p align="center"><img src="docs/screenshots/multimapping.png" width="100%" alt="Multimapping" /></p>
<ul>
<li>Create a room or join with a code; live cursors for all participants</li>
<li>Full editor sync: tiles, layers, quads, groups, images, sounds</li>
<li>Map transfer for late join; <strong>MULTIMAPPING</strong> badge on the Editor button</li>
</ul>
</td>
</tr>
<tr>
<td width="50%" valign="top" align="left">
<strong>HUD Editor</strong><br><br>
<p align="center"><img src="docs/screenshots/hud_editor.jpg" width="100%" alt="HUD Editor" /></p>
<ul>
<li>Drag, drop, and resize HUD modules in real time</li>
<li>Chat, votes, music player, keystrokes, voice HUD, and more</li>
<li>Per-module reset and animated Reset All</li>
</ul>
</td>
<td width="50%" valign="top" align="left">
<strong>Fast Practice</strong><br><br>
<p align="center"><img src="docs/screenshots/fast_practice.jpg" width="100%" alt="Fast Practice" /></p>
<ul>
<li>Toggle from ingame menu or <code>fast_practice_toggle</code></li>
<li>Practice chat commands: <code>/rescue</code>, <code>/tp</code>, <code>/weapon</code>, <code>/invincible</code>, and more</li>
<li>Ghost tee and anchor saving; requires a dummy on the server</li>
</ul>
</td>
</tr>
</table>

## Feature overview

### Visuals

- **Chat Media** — inline photo/GIF previews, domain filter, Gif Wheel, CherryGifs, above-head gif bubble
- **Gradient** — gradient nicknames, clans, skins, or all text (skin / custom / rainbow)
- **Hook Combo** — combo popups and sounds (hook / hammer / both)
- **Jelly Tee** — tee deformation (self / others)
- **3D Particles** — cubes/hearts/mixed, glow, player push
- **Media Background** — video/image in menus and ingame background
- **Eye Comfort** — warm screen overlay
- **Sweat Weapon (Crystal Laser)** — crystal laser and shotgun effects
- **Flying Name Plates** — kite-style trailing name plates
- **Motion Blur** — previous-frame blending (BETA)
- **Animations** — UI reveal, chat/killfeed/main menu animations
- **Music Player** — HUD player with visualizer and cover colors
- **Keystrokes** — Classic / Minecraft keyboard and mouse HUD
- **Custom Aspect Ratio** — presets and arbitrary num:den input
- **Chat Bubbles** — bubbles above players

Demo playback only (Demo Player): **Camera Drift**, **Dynamic FOV**.

### Gameplay

- **Inputs** — Fast, Best (smoothing/latency/interpolation), Saiko, Delta, F + Auto Margin
- **Snap Tap** — instant opposite left/right switching (with community blocklist)
- **Optimizer** — disable particles, FPS Fog, process priorities
- **Gores mode** — entity-like mode with auto-disable while holding weapons
- **Self timeCP** — personal checkpoints (tee / cursor placement)
- **Fast Actions** — quick command selector
- **Speedrun timer** — H/M/S/ms timer
- **Finish Prediction** — finish progress prediction on race maps
- **Focus Mode** — hide distracting HUD/UI elements

### Others

- **Misc** — auto update, layout, silent typing, cinematic camera, spec moved notify, extend zoom, UI/wheel/scoreboard scale, and more
- **Rollback Demo** — replay rollback with configurable length
- **Browser Utils** — auto server list refresh, short KoG server names
- **Chat Filter** — regex/censor list, player whitelist
- **Voice Binds** — push-to-talk and voice moderation key
- **Voice Chat** — Opus PTT/VAD, radius/team0, per-player volume/mute, voice mod tab in admin panel
- **Client Indicator** — BestClient icon in nameplates/scoreboard + browser filter
- **Admin Panel** — say/broadcast/mute/ban/kick/respawn/pause + voice moderation
- **Translate** — incoming/outgoing chat translation (button in chat UI)
- **Clans** — register/login, create or join clans, roles, applications, announcements, online status, in-game clan tag sync
- **Multimapping** — collaborative map editing with room codes, live cursors, full editor sync, map transfer for late join
- **Fast Practice** — local practice world with dummy ghost, practice chat commands, `fast_practice_toggle`
- **Assets Editor** — mix assets, prepare exports, jump into the name plate workflow from a fullscreen editor
- **Fun tab mini-games** — Snake, Minesweeper, Chess, Memory, Pong, Brick Breaker

## Build

Initialize submodules first:

```bash
git submodule update --init --recursive
```

Quick build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDEV=ON -DVULKAN=ON
cmake --build build --target everything
```

CI-like build:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DDOWNLOAD_GTEST=ON -DDEV=ON
cmake --build build --target everything
```

Run tests:

```bash
cmake --build build --target run_tests
```

Headless test build:

```bash
cmake -S . -B headless -G Ninja -DHEADLESS_CLIENT=ON -DCMAKE_BUILD_TYPE=Debug -DDOWNLOAD_GTEST=ON -DDEV=ON
cmake --build headless --target run_tests
```

## Full `bc_*` config list

Source: `src/engine/shared/config_variables_bestclient.h` (278 variables).

<details>
<summary>Expand full list</summary>

```cfg
bc_3d_particles
bc_3d_particles_alpha
bc_3d_particles_collide
bc_3d_particles_color
bc_3d_particles_color_mode
bc_3d_particles_count
bc_3d_particles_depth
bc_3d_particles_fade_in_ms
bc_3d_particles_fade_out_ms
bc_3d_particles_glow
bc_3d_particles_glow_alpha
bc_3d_particles_glow_offset
bc_3d_particles_push_radius
bc_3d_particles_push_strength
bc_3d_particles_size_max
bc_3d_particles_size_min
bc_3d_particles_speed
bc_3d_particles_type
bc_3d_particles_view_margin
bc_animations
bc_auto_margin
bc_auto_server_list_refresh
bc_auto_server_list_refresh_seconds
bc_auto_team_lock
bc_auto_team_lock_delay
bc_auto_update
bc_best_input_amount
bc_best_input_interpolation
bc_best_input_latency_comp
bc_best_input_others
bc_best_input_smoothing
bc_bestclient_settings_tabs
bc_blocked_content_partial_replacement_char
bc_blocked_content_replacement_char
bc_blocked_word_console_color
bc_camera_drift
bc_camera_drift_amount
bc_camera_drift_reverse
bc_camera_drift_smoothness
bc_casino_balance
bc_casino_last_claim
bc_chat_alt_command_layout
bc_chat_animation
bc_chat_animation_ms
bc_chat_animation_type
bc_chat_bubble_animation
bc_chat_bubble_bg_color
bc_chat_bubble_custom_colors
bc_chat_bubble_fadein
bc_chat_bubble_fadeout
bc_chat_bubble_outline_color
bc_chat_bubble_rounding
bc_chat_bubble_showtime
bc_chat_bubble_size
bc_chat_bubble_text_color
bc_chat_bubbles
bc_chat_bubbles_demo
bc_chat_bubbles_self
bc_chat_media_allowed_domains
bc_chat_media_content_filter
bc_chat_media_gifs
bc_chat_media_photos
bc_chat_media_preview
bc_chat_media_preview_max_width
bc_chat_open_animation
bc_chat_open_animation_ms
bc_chat_save_draft
bc_chat_typing_animation
bc_chat_typing_animation_ms
bc_cherrygifs_show_nsfw
bc_cherrygifs_sort_top
bc_cinematic_camera
bc_clans_allow_local_dev
bc_clans_api_url
bc_clans_enabled
bc_clans_unread_badge
bc_client_indicator
bc_client_indicator_browser_url
bc_client_indicator_in_name_plate
bc_client_indicator_in_name_plate_above_self
bc_client_indicator_in_name_plate_dynamic
bc_client_indicator_in_name_plate_size
bc_client_indicator_in_scoreboard
bc_client_indicator_in_scoreboard_size
bc_client_indicator_secret_key
bc_client_indicator_server_address
bc_client_indicator_shared_token
bc_client_indicator_token_url
bc_client_indicator_versions
bc_crystal_laser
bc_custom_aspect_ratio
bc_custom_aspect_ratio_apply_mode
bc_custom_aspect_ratio_den
bc_custom_aspect_ratio_mode
bc_custom_aspect_ratio_num
bc_delta_input_amount
bc_delta_input_others
bc_dynamic_fov
bc_dynamic_fov_amount
bc_dynamic_fov_smoothness
bc_enable_censor_list
bc_extend_zoom
bc_eye_comfort
bc_eye_comfort_strength
bc_f_input_amount
bc_f_input_others
bc_fast_actions
bc_filter_change_whole_word
bc_finish_prediction
bc_finish_prediction_show_always
bc_finish_prediction_show_millis
bc_finish_prediction_show_percentage
bc_finish_prediction_show_time
bc_finish_prediction_time_mode
bc_flying_name_plates
bc_flying_name_plates_drag
bc_flying_name_plates_follow
bc_flying_name_plates_lift
bc_game_media_background
bc_game_media_background_offset
bc_gif_bubble_above_head
bc_gif_bubble_domains
bc_gif_bubble_duration_ms
bc_gif_bubble_offset_y
bc_gores_mode
bc_gores_mode_disable_weapons
bc_hide_hud_in_settings
bc_hook_combo
bc_hook_combo_mode
bc_hook_combo_reset_time
bc_hook_combo_size
bc_hook_combo_sound_volume
bc_hud_chat_scale
bc_hud_chat_x
bc_hud_chat_y
bc_hud_music_player_scale
bc_hud_music_player_x
bc_hud_music_player_y
bc_hud_voice_hud_scale
bc_hud_voice_hud_x
bc_hud_voice_hud_y
bc_hud_voice_mute_icons_scale
bc_hud_voice_mute_icons_x
bc_hud_voice_mute_icons_y
bc_hud_votes_scale
bc_hud_votes_x
bc_hud_votes_y
bc_inputs
bc_jelly_tee
bc_jelly_tee_duration
bc_jelly_tee_others
bc_jelly_tee_strength
bc_keystrokes_keyboard
bc_keystrokes_keyboard_preset
bc_keystrokes_mc_layout
bc_keystrokes_mc_show_lmb
bc_keystrokes_mc_show_rmb
bc_keystrokes_mc_show_space
bc_keystrokes_mc_show_ws
bc_keystrokes_mouse
bc_keystrokes_mouse_preset
bc_keystrokes_style
bc_killfeed_animation
bc_killfeed_animation_ms
bc_main_menu_animation
bc_main_menu_animation_speed
bc_mastersrv
bc_menu_media_background
bc_menu_media_background_path
bc_module_ui_reveal_animation
bc_module_ui_reveal_animation_ms
bc_motion_blur
bc_motion_blur_strength
bc_multiple_replacement_char
bc_music_player
bc_music_player_animation_ms
bc_music_player_color_mode
bc_music_player_hud_color_alpha
bc_music_player_show_cover
bc_music_player_show_when_paused
bc_music_player_size_mode
bc_music_player_static_color
bc_music_player_text_scale
bc_music_player_use_color_for_hud
bc_music_player_visualizer
bc_music_player_visualizer_column_width
bc_music_player_visualizer_columns
bc_music_player_visualizer_gap
bc_music_player_visualizer_mode
bc_music_player_visualizer_rounding
bc_music_player_visualizer_sensitivity
bc_music_player_visualizer_smoothing
bc_mute_others_hammer
bc_mute_others_hook
bc_nameplate_client_indicator_offset_x
bc_nameplate_client_indicator_offset_y
bc_nameplate_gradient
bc_nameplate_gradient_animate_speed
bc_nameplate_gradient_clan
bc_nameplate_gradient_color1
bc_nameplate_gradient_color2
bc_nameplate_gradient_color3
bc_nameplate_gradient_color4
bc_nameplate_gradient_color_count
bc_nameplate_gradient_everything
bc_nameplate_gradient_mode
bc_nameplate_gradient_skin
bc_nameplate_voice_offset_x
bc_nameplate_voice_offset_y
bc_optimizer
bc_optimizer_ddnet_priority_high
bc_optimizer_disable_particles
bc_optimizer_discord_priority_below_normal
bc_optimizer_fps_fog
bc_optimizer_fps_fog_cull_map_tiles
bc_optimizer_fps_fog_mode
bc_optimizer_fps_fog_radius_tiles
bc_optimizer_fps_fog_render_rect
bc_optimizer_fps_fog_zoom_percent
bc_prev_inp_mousesens_45_degrees
bc_prev_inp_mousesens_small_sens
bc_prev_mouse_max_distance_45_degrees
bc_regex_player_whitelist
bc_saiko_input_amount
bc_saiko_input_others
bc_scoreboard_scale
bc_scoreboard_team_gradients
bc_self_time_cp
bc_self_time_cp_color
bc_self_time_cp_place_mode
bc_settings_layout
bc_show_blocked_word_in_console
bc_show_correct_checkpoint
bc_show_real_hitbox
bc_show_real_hitbox_color
bc_showhud_dummy_coord_indicator
bc_silent_typing
bc_snap_tap
bc_snap_tap_delay
bc_spec_moved_notify
bc_spec_moved_notify_text
bc_speedrun_timer
bc_speedrun_timer_auto_disable
bc_speedrun_timer_hours
bc_speedrun_timer_milliseconds
bc_speedrun_timer_minutes
bc_speedrun_timer_seconds
bc_toggle_45_degrees
bc_toggle_small_sens
bc_translate_incoming_ignore_languages
bc_translate_incoming_source
bc_translate_outgoing_source
bc_translate_outgoing_strip_punctuation
bc_translate_outgoing_target
bc_use_short_kog_server_name
bc_voice_chat_activation_mode
bc_voice_chat_bitrate
bc_voice_chat_enable
bc_voice_chat_enable_your_group
bc_voice_chat_headphones_muted
bc_voice_chat_ingame_only
bc_voice_chat_input_device
bc_voice_chat_mic_check
bc_voice_chat_mic_gain
bc_voice_chat_mic_muted
bc_voice_chat_muted_names
bc_voice_chat_name_volumes
bc_voice_chat_nameplate_icon
bc_voice_chat_output_device
bc_voice_chat_radius_enabled
bc_voice_chat_radius_tiles
bc_voice_chat_server_address
bc_voice_chat_use_team0
bc_voice_chat_vad_release_delay_ms
bc_voice_chat_vad_threshold
bc_voice_chat_volume
bc_voice_mod_key
bc_wheel_scale
```

</details>

Plain-text export: [docs/bc_config_list.txt](docs/bc_config_list.txt).

## License

BestClient-authored code is released under the **[MIT License](LICENSE)**.

Upstream DDNet/Teeworlds code, third-party libraries, and assets under `data/` remain under their original licenses — see [license.txt](license.txt) and per-directory `license.txt` files.
