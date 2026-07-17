<div align="center">
  <img src="https://badges4noxy.vercel.app/badges/bestclient/bc_logo.webp" alt="BestClient" width="640" />
  <p>Custom DDNet client forked from Tater, focused on comfort, customization, native tooling, and competitive quality-of-life features.</p>

  <p>
    <a href="https://bestclient.fun"><img src="https://badges4noxy.vercel.app/badges/bestclient/website.svg" alt="Website"/></a>
    <a href="https://t.me/bestddnet"><img src="https://badges4noxy.vercel.app/badges/bestclient/telegram.svg" alt="Telegram"/></a>
    <a href="https://discord.gg/bestclient"><img src="https://badges4noxy.vercel.app/badges/bestclient/discord.svg" alt="Discord"/></a>
  </p>
</div>

## About

BestClient is a customized DDNet client built around native BestClient systems instead of a thin reskin.
The project adds its own rendering stack, input tools, social systems, editors, in-client content flow, and UI modules while staying in the DDNet ecosystem.

Today the client ships with 280 `bc_*` config entries, 52 component toggles in the Components editor, a dedicated ReShade tab, built-in voice and media systems, live HUD tooling, an in-client asset shop, and a curated Fun tab.

## Links

- Website: [bestclient.fun](https://bestclient.fun)
- Telegram: [t.me/bestddnet](https://t.me/bestddnet)
- Discord: [discord.gg/bestclient](https://discord.gg/bestclient)

## Highlights

- Native visual stack with Camera Drift, Dynamic FOV, Cinematic Camera, Jelly Tee, Afterimage, Motion Blur, Player Trail, Crystal Laser, Flying Name Plates, Eye Comfort, and multiple particle systems.
- Input and gameplay tooling with Fast Input, Delta Input, Best Input, Saiko+, Snap Tap, 45 Degrees, Small Sens, Hook Combo, Fast Actions, Speedrun Timer, Finish Prediction, Auto Team Lock, and Focus Mode.
- Social and utility systems such as Chat Media, integrated Voice Chat, Client Indicator, Admin Panel, Music Player, Browser utilities, streamer helpers, and BestClient browser integrations.
- Custom native tooling including the Assets editor, Components editor, HUD editor, ReShade controls, and live module placement for HUD widgets.
- Built-in content flow through the BestClient Shop and the Fun tab with bundled mini-games.

## Feature Overview

### Visuals

- Camera Drift
- Dynamic FOV
- Cinematic Camera
- Custom Aspect Ratio
- Jelly Tee
- Afterimage
- Motion Blur
- Player Trail
- Crystal Laser
- Magic Particles
- Orbit Aura
- 3D Particles
- Flying Name Plates
- Eye Comfort
- Chat Bubbles
- Music Player with visualizer and HUD integration
- Media Background for menu and game
- ReShade runtime integration and live shader UI
- UI, chat, killfeed, module reveal, and in-game menu animations
- Emoticon Shadow, Dummy Coord Indicator, Real Hitbox Dot, Scoreboard Team Gradients, and points-in-tab helpers

### Gameplay And Input

- Fast Input
- Delta Input
- Best Input
- Saiko+
- Snap Tap
- 45 Degrees
- Small Sens
- Keystrokes HUD
- Hook Combo
- Fast Actions
- Speedrun Timer
- Finish Prediction
- Auto Team Lock
- Gores Mode
- Optimizer and FPS Fog
- Focus Mode
- ESC player list enhancements

### Social, Browser, And Utility

- Chat Media preview and viewer
- Voice Chat
- Voice Binds
- Client Indicator
- Admin Panel
- Streamer Flags
- Menu SFX
- BestClient master server mirror
- Auto-refresh server browser
- Short KoG server names
- Browser filtering for BestClient users
- Config helpers and update flow

## Editors

### Assets Editor

Mix assets, prepare exports, and jump into the name plate workflow from a native fullscreen editor.

### Components Editor

The Components editor is a dedicated fullscreen toggle page for BestClient and TClient modules.
It stages changes before apply, keeps track of unsaved edits, warns before discarding them, and prompts for restart after applying the new component mask.

Current groups:

- Visuals
- Gameplay
- Others
- TClient

Current toggle coverage includes BestClient modules such as Camera Drift, Jelly Tee, Magic Particles, Dynamic FOV, Afterimage, Music Player, Media Background, Eye Comfort, Input, Fast Actions, Finish Prediction, Voice Chat, Voice Binds, and Chat Media, plus TClient tabs/pages/settings entries.

### HUD Editor

Move HUD modules directly over live gameplay or demo playback, including music player, voice HUD, mute icons, chat, and votes.

## Fun And Shop

The Fun tab currently exposes these mini-games in the UI:

- Snake
- Minesweeper
- 2048
- Chess
- Tic-Tac-Toe
- Lights Out
- Memory
- BlockBlast
- Tetris
- Pong
- Packman
- Flappy Bird
- Cat Trap
- Brick Breaker

The Shop tab provides in-client browsing and installation for:

- Entities
- Game
- Emoticons
- Particles
- HUD
- Arrows
- Cursors
- Audio

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

## Full `bc_*` Config List

```cfg
bc_camera_drift
bc_camera_drift_amount
bc_camera_drift_smoothness
bc_camera_drift_reverse
bc_dynamic_fov
bc_dynamic_fov_amount
bc_dynamic_fov_smoothness
bc_custom_aspect_ratio_mode
bc_custom_aspect_ratio_apply_mode
bc_custom_aspect_ratio
bc_chat_media_preview
bc_chat_media_photos
bc_chat_media_gifs
bc_chat_media_content_filter
bc_chat_media_allowed_domains
bc_chat_media_preview_max_width
bc_chat_media_viewer
bc_chat_media_viewer_max_zoom
bc_crystal_laser
bc_prev_mouse_max_distance_45_degrees
bc_prev_inp_mousesens_45_degrees
bc_toggle_45_degrees
bc_prev_inp_mousesens_small_sens
bc_toggle_small_sens
bc_gores_mode
bc_gores_mode_disable_weapons
bc_hook_combo
bc_hook_combo_mode
bc_hook_combo_reset_time
bc_hook_combo_sound_volume
bc_hook_combo_size
bc_cinematic_camera
bc_animations
bc_module_ui_reveal_animation
bc_module_ui_reveal_animation_ms
bc_ingame_menu_animation
bc_ingame_menu_animation_ms
bc_chat_animation
bc_chat_animation_ms
bc_chat_open_animation
bc_chat_open_animation_ms
bc_chat_typing_animation
bc_chat_typing_animation_ms
bc_killfeed_animation
bc_killfeed_animation_ms
bc_chat_animation_type
bc_settings_layout
bc_reshade_enabled
bc_reshade_notice_do_not_show_again
bc_reshade_auto_accept
bc_reshade_show_only_enabled
bc_hide_hud_in_settings
bc_eye_comfort
bc_eye_comfort_strength
bc_esc_player_list
bc_show_points_in_tab
bc_bestclient_settings_tabs
bc_emoticon_shadow
bc_chat_save_draft
bc_chat_alt_command_layout
bc_scoreboard_team_gradients
bc_showhud_dummy_coord_indicator
bc_showhud_dummy_coord_indicator_color
bc_showhud_dummy_coord_indicator_same_height_color
bc_show_real_hitbox
bc_show_real_hitbox_color
bc_auto_server_list_refresh
bc_auto_server_list_refresh_seconds
bc_mastersrv
bc_use_short_kog_server_name
bc_streamer_flags
bc_fast_input_mode
bc_fast_input_delta_input
bc_fast_input_gamma_input
bc_saiko_plus_amount
bc_best_input_preset
bc_best_input_offset
bc_best_input_smoothing
bc_best_input_latency_comp
bc_best_input_interpolation
bc_delta_input_others
bc_gamma_input_others
bc_best_input_others
bc_saiko_plus_others
bc_fast_input_auto_margin
bc_snap_tap
bc_snap_tap_delay
bc_keystrokes_keyboard
bc_keystrokes_keyboard_preset
bc_keystrokes_mouse
bc_keystrokes_mouse_preset
bc_auto_team_lock
bc_auto_team_lock_delay
bc_speedrun_timer
bc_speedrun_timer_time
bc_speedrun_timer_hours
bc_speedrun_timer_minutes
bc_speedrun_timer_seconds
bc_speedrun_timer_milliseconds
bc_speedrun_timer_auto_disable
bc_finish_prediction
bc_finish_prediction_mode
bc_finish_prediction_show_always
bc_finish_prediction_time_mode
bc_finish_prediction_show_time
bc_finish_prediction_show_percentage
bc_finish_prediction_show_millis
bc_finish_prediction_bar_custom_color
bc_finish_prediction_bar_color
bc_adminpanel_autoscroll
bc_adminpanel_remember_tab
bc_adminpanel_last_tab
bc_adminpanel_disable_anim
bc_adminpanel_scale
bc_adminpanel_log_lines
bc_adminpanel_bg_color
bc_adminpanel_tab_inactive_color
bc_adminpanel_tab_active_color
bc_adminpanel_tab_hover_color
bc_admin_fast_action0
bc_admin_fast_action1
bc_admin_fast_action2
bc_admin_fast_action3
bc_admin_fast_action4
bc_admin_fast_action5
bc_admin_fast_action6
bc_admin_fast_action7
bc_admin_fast_action8
bc_admin_fast_action9
bc_music_player
bc_music_player_show_when_paused
bc_music_player_visualizer
bc_music_player_visualizer_mode
bc_music_player_visualizer_sensitivity
bc_music_player_visualizer_smoothing
bc_music_player_visualizer_rounding
bc_music_player_visualizer_columns
bc_music_player_visualizer_column_width
bc_music_player_visualizer_gap
bc_music_player_color_mode
bc_music_player_static_color
bc_music_player_size_mode
bc_music_player_text_scale
bc_music_player_animation_ms
bc_music_player_show_cover
bc_music_player_use_color_for_hud
bc_music_player_hud_color_alpha
bc_hud_music_player_x
bc_hud_music_player_y
bc_hud_music_player_scale
bc_hud_voice_hud_x
bc_hud_voice_hud_y
bc_hud_voice_hud_scale
bc_hud_voice_mute_icons_x
bc_hud_voice_mute_icons_y
bc_hud_voice_mute_icons_scale
bc_hud_chat_x
bc_hud_chat_y
bc_hud_chat_scale
bc_hud_votes_x
bc_hud_votes_y
bc_hud_votes_scale
bc_disabled_components_mask_lo
bc_disabled_components_mask_hi
bc_voice_chat_enable
bc_voice_chat_activation_mode
bc_voice_chat_vad_threshold
bc_voice_chat_vad_release_delay_ms
bc_voice_chat_volume
bc_voice_chat_mic_gain
bc_voice_chat_bitrate
bc_voice_chat_input_device
bc_voice_chat_output_device
bc_voice_chat_mic_muted
bc_voice_chat_headphones_muted
bc_voice_chat_mic_check
bc_voice_chat_ingame_only
bc_voice_chat_use_team0
bc_voice_chat_enable_your_group
bc_voice_chat_radius_enabled
bc_voice_chat_radius_tiles
bc_voice_chat_nameplate_icon
bc_voice_chat_server_address
bc_voice_chat_muted_names
bc_voice_chat_name_volumes
bc_menu_sfx
bc_menu_sfx_volume
bc_menu_media_background
bc_game_media_background
bc_menu_media_background_path
bc_game_media_background_offset
bc_shop_auto_set
bc_afterimage
bc_afterimage_frames
bc_afterimage_alpha
bc_afterimage_spacing
bc_motion_blur
bc_motion_blur_strength
bc_jelly_tee
bc_jelly_tee_others
bc_jelly_tee_strength
bc_jelly_tee_duration
bc_trail
bc_trail_others
bc_trail_mode
bc_chat_bubbles
bc_chat_bubbles_self
bc_chat_bubbles_demo
bc_chat_bubble_size
bc_chat_bubble_showtime
bc_chat_bubble_fadeout
bc_chat_bubble_fadein
bc_chat_bubble_animation
bc_chat_bubble_custom_colors
bc_chat_bubble_bg_color
bc_chat_bubble_text_color
bc_chat_bubble_outline_color
bc_chat_bubble_rounding
bc_client_indicator
bc_nameplate_voice_offset_x
bc_nameplate_voice_offset_y
bc_nameplate_client_indicator_offset_x
bc_nameplate_client_indicator_offset_y
bc_flying_name_plates
bc_flying_name_plates_lift
bc_flying_name_plates_drag
bc_flying_name_plates_follow
bc_client_indicator_in_name_plate
bc_client_indicator_in_name_plate_above_self
bc_client_indicator_in_name_plate_size
bc_client_indicator_in_name_plate_dynamic
bc_client_indicator_in_scoreboard
bc_client_indicator_in_scoreboard_size
bc_client_indicator_server_address
bc_client_indicator_browser_url
bc_client_indicator_token_url
bc_client_indicator_shared_token
bc_client_indicator_secret_key
bc_magic_particles
bc_magic_particles_radius
bc_magic_particles_size
bc_magic_particles_alpha_delay
bc_magic_particles_type
bc_magic_particles_count
bc_orbit_aura
bc_orbit_aura_radius
bc_orbit_aura_particles
bc_orbit_aura_alpha
bc_orbit_aura_speed
bc_orbit_aura_idle
bc_orbit_aura_idle_timer
bc_optimizer
bc_optimizer_disable_particles
bc_optimizer_fps_fog
bc_optimizer_ddnet_priority_high
bc_optimizer_discord_priority_below_normal
bc_optimizer_fps_fog_mode
bc_optimizer_fps_fog_radius_tiles
bc_optimizer_fps_fog_zoom_percent
bc_optimizer_fps_fog_render_rect
bc_optimizer_fps_fog_cull_map_tiles
bc_3d_particles
bc_3d_particles_type
bc_3d_particles_count
bc_3d_particles_size_min
bc_3d_particles_size_max
bc_3d_particles_speed
bc_3d_particles_depth
bc_3d_particles_alpha
bc_3d_particles_fade_in_ms
bc_3d_particles_fade_out_ms
bc_3d_particles_push_radius
bc_3d_particles_push_strength
bc_3d_particles_collide
bc_3d_particles_view_margin
bc_3d_particles_color_mode
bc_3d_particles_color
bc_3d_particles_glow
bc_3d_particles_glow_alpha
bc_3d_particles_glow_offset
```

## License

BestClient uses a mixed-license model.
Upstream DDNet/Teeworlds code, third-party libraries, and per-directory data licenses stay under their original terms.

The BestProject-authored permission-required scope is defined in:

- [LICENSE](LICENSE)
- [docs/BESTCLIENT_AUTHORED_FUNCTIONS.md](docs/BESTCLIENT_AUTHORED_FUNCTIONS.md)

The authored-scope document is the authoritative registry for current BestClient `bc_*` settings, including the component-mask settings used by the Components editor.
