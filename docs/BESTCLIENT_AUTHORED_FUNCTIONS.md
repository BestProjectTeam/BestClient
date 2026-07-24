# BestClient Authored Code Reference

BestClient-authored client code is released under the [MIT License](../LICENSE).

Upstream DDNet/Teeworlds code, third-party libraries, and per-directory data
assets remain under their original licenses. See `license.txt` in the repository
root.

## Primary authored areas

- `src/game/client/components/bestclient/` — main BestClient feature implementations
- `src/game/client/components/menus_bestclient.cpp` — BestClient settings UI
- `src/engine/shared/config_variables_bestclient.h` — `bc_*` client config variables
- `src/game/client/components/menu_media_background.*` and `media_decoder.*`
- `src/game/editor/multimapping/` — collaborative map editor sessions
- `bestclient-voice-server/` — voice relay server (Rust)

## BestClient settings registry (`bc_*`)

Source of truth: `src/engine/shared/config_variables_bestclient.h`.

Machine-readable export: [bc_config_list.txt](bc_config_list.txt) (278 entries).

```cfg
# See bc_config_list.txt for the full sorted list.
```
