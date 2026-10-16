# Entity tile rounding

The client can round the contour of solid/nohook, freeze/unfreeze, death and numbered teleporter regions. This changes rendering only; tile IDs, maps, collision and prediction are unchanged.

## Settings

The controls are in TClient settings, under **Tile Outlines**. They are also available in the console and saved in the client configuration:

| Setting | Default | Values |
| --- | --- | --- |
| `bc_entities_rounding` | `0` | `0`–`100` percent; `0` disables rounding |
| `bc_entities_rounding_mode` | `2` | `0`: outer corners, `1`: inner corners, `2`: both |

For example:

```text
bc_entities_rounding 75
bc_entities_rounding_mode 2
```

The maximum radius is half a tile (16 world units). At 100%, an isolated tile is a circle when outer corners are enabled; inner-only mode keeps it square. Connected regions retain continuous straight edges. Diagonal contacts remain separate. Existing outline enable, color, width and entities-only settings still apply. Switch tile artwork follows the visible entity contour when it overlaps a rounded region. Decoration, speedup arrows and tune tiles retain their original geometry. Number overlays remain text and are scaled into rounded outer corners.

## Rendering

`CEntityRegions` supplies the same validated category map to entity layers and outlines, retaining the existing layer and category priorities. A separate region key includes the visible tele/switch asset type and the category of the buried layer. At a three-against-one junction between different visible categories, the lone tile cuts a circular corner and the diagonal neighbor draws the complementary wedge using its own asset. A covered lower-layer tile stays square beneath the curved visible tile, so its artwork fills exposed space. Both sides share one arc, and only the higher-priority category draws its outline. Other mixed-category seams remain straight; different tele/switch assets and buried-underlay changes do not create a spurious curve. `RoundedTiles` builds a subdivision mesh only around affected corners. Outer corners warp the whole picture into the curve; open-sky inner corners keep artwork at its original map coordinates and extend edge pixels into the added area. Tile rotation and reflection remain supported.

Buffered tile vertices use floating-point texture coordinates. Each map cell records its own quad count. The OpenGL and Vulkan tile/border layouts agree with the uploaded vertex format. The unbuffered path uses the same shape generator, with freeform texture-array draws or atlas coordinates as supported by the backend.

Shapes are cached by neighborhood. Buffered tiles use a fixed four-step corner mesh, while high zoom draws only visible tiles at the screen-space detail needed there. Zoom-only detail changes no longer re-upload every map tile. Radius and mode changes still rebuild the buffered geometry. Atlas selection is resolved by the existing texture path and does not require rebuilding UVs. Culling includes the neighboring cell so expanded junctions do not disappear at screen edges.

Outlines use the distance band inside the same contour, clipped to a disjoint tessellation to avoid double alpha at joins. Cached quad containers are reused by neighborhood and width. Straight walls use a smaller rectangular mesh. A boundary index built on map load visits only visible contour cells, rather than rescanning the full viewport every frame. Repeated contours outside map borders use scaled straight strips. Map changes, shutdown and rounding changes release the containers.

Front-layer through/through-cut artwork and the entity tile beneath it share the same rounded silhouette. A complementary inner wedge samples the through asset in the diagonal cell's local coordinates. This continues its repeating stripe pattern into the added area instead of stretching a clamped edge texel. Straight outline segments ending at an adjacent arc use flat caps. The signed cap distance remains continuous while clipping mesh cells, preserving the last part of the line without an endpoint spur. The two side tiles also retain that arc as an outline distance source, while their geometry stays square. This fills the inward part of the curved band on both sides of the tangent without overlapping tile meshes; material priority still determines who draws a shared arc. Number overlays use one size for all tiles at the selected radius, so an interior label does not grow relative to labels on the rounded ends.

## Verification

`src/test/rounded_tiles_test.cpp` is registered with the existing GTest runner. It covers disabled geometry, the maximum-radius circle, full UV coverage, concave junction area, all neighborhood patterns and modes, shared-edge continuity, outline area and the detail error bound.

Run the project's `testrunner` with `--gtest_filter=RoundedTiles.*:EntityRegions.*:EntityOutlineIndex.*`. Build `game-client` with `VULKAN=ON` to validate both compiled backends and Vulkan shaders.

Verified on 2026-09-22:

- Release `game-client` and the local test server build successfully. The client build reports zero warnings and errors.
- The original eight `RoundedTiles` tests pass when compiled with MSVC and the project's pinned GTest sources. GTest is not installed globally on this machine, so this run used a standalone test executable.
- OpenGL 3.3, OpenGL 1.5 (legacy unbuffered rendering), and Vulkan successfully connect to a local `Tutorial` server with 100% rounding, both corner modes and all outline categories enabled. Each run exits normally after 20 seconds with no graphics errors in the client log. Test hardware: NVIDIA GeForce GTX 1650 SUPER.
- OpenGL 2.1 also passes a 15-second run at 50% with inner corners only; OpenGL 3.3 passes with rounding disabled. Both connect to the same map and exit normally without graphics errors.
- A software rasterization of the actual shape and outline generator was inspected for 0/50/100% and modes 0/1/2. This checks the mesh silhouette, not GPU screenshots.

The local verification files are in the ignored `build-rounding` directory: `rounded-tests.exe`, `rounding-preview.png`, build logs and `runtime` profiles/logs. The test profile uses its own `storage.cfg`; it does not load or overwrite the user's settings. Launching the Visual Studio output directly requires DLLs from the build root and data/shader paths in storage. After removing generated shader files, reconfigure with `-U VULKAN_SHADER_FILE_SHA256` to regenerate the Vulkan shader cache.

Further visual checks remain for the user's exact map and atlas, map borders, FPS fog and live setting changes. Window automation was unavailable in this environment, so game screenshots were captured directly through a temporary graphics hook. These short runs are not a controlled performance or GPU-memory benchmark. Compare these metrics on the same map and camera before and after enabling rounding.

On 2026-09-23, nine game screenshots per backend were captured with a temporary test hook on a local fixture map. The captures cover 0%, 50%, 100%; outer, inner and combined modes; outline widths 2 and 16; and outlines off. OpenGL 3.3, Vulkan and unbuffered OpenGL 1.5 all rendered the expected silhouettes. The entity regions in corresponding Vulkan and OpenGL 3.3 images were pixel-identical in six sampled scene rectangles for all nine cases. View the local image gallery at `build-rounding/rounding-visual-check.html`. The capture hook is removed from the delivered source; the generated fixture and images stay in the ignored build directory.

A second fixture placed different entity categories beside and on top of each other at high zoom. It exposed a mismatch between the visible tile and outline plus a triangular outline spike at a category junction. The geometry now uses the same exact-category neighborhood for both, blocks curves at occupied neighboring categories and applies outline priority only to the shared edge. The overlap capture shows switch and numbered teleporter artwork following the rounded silhouette at 100%, a square silhouette at 0% and the same tile silhouette with outlines disabled.

Further captures with numbered teleporter L-junctions reproduced oversized numbers and a sharp diagonal from the asset's internal frame. Numbers now fit within rounded outer corners. The inner patch keeps artwork in the original cell and extends its edge texels into the curved addition; the large diagonal artifact is gone. A synthetic 256-neighborhood outline benchmark dropped from about 7.5 to 1.8 seconds at 16 steps and from 57 to 15.5 seconds at 32 steps after skipping interior mesh patches. This is a construction benchmark, not a measured in-game frame time.

Verified on 2026-09-24 with the user's Felian map at cell (421,132): screenshots in `build-rounding/runtime/screenshots/felian-center-421-132-asset-*` cover 0%, 50%, 100%, all three modes, and outline widths 0, 2, and 16. The former olive corner fragments at a tele tile over unfreeze are gone. At the neighboring tele-type seam, red pixels more than two screen pixels outside the tile boundary dropped to zero. All 18 focused geometry/region tests pass. These captures used the bundled default entity artwork; a custom entity pack may need separate visual verification.

The later user screenshot exposed a separate discontinuity at stair junctions: with an inner arc disabled, straight outline bands in neighboring tiles touch only diagonally. The outline now inserts a join source in the third tile at that specific three-against-one topology, provided the diagonal region does not own the outline. A 4-connected raster test at four pixels per world unit reproduces two components without the join and one with it. All 20 focused tests pass. The client's Release build succeeds; the exact custom-artwork location has not been re-captured.

The covered material junction now uses complementary geometry instead of suppressing both corners. A raster partition test checks that two materials cover every sampled point exactly once at three radii and all three modes. The two straight outlines next to the curve stop at its tangents, and only one material owns the arc. All 24 focused tests pass. A software preview in `build-rounding/covered-preview.png` shows the same staircase at 0%, 50%, and 100%.

The later jagged-edge regression had two causes at an open-sky three-against-one junction. The diagonal tile bent its whole corner mesh across both side tiles, and the outline fast path widened a shortened straight segment into a full-width band. Now the diagonal tile retains its original square and adds only the circular wedge in the empty cell; the two side tiles stop their contours at the arc tangents. The rectangular outline fast path is restricted to complete tile sides. A sampled partition test and a tangent-extent test cover these failures. All 27 focused tests pass. Close-up screenshots from the game client at 100% in all three modes and at 50% are in `build-rounding/runtime/screenshots/qa-close-*`; Felian checks are in `qa-felian-*` in the same directory. The temporary inactive-window screenshot allowance used for these captures is removed from the source.

Verified on 2026-09-28: 32 focused tests pass, including repeating through-hook wedge UVs, a fractional-radius straight cap, and the arc's inward band in both side tiles. The side-tile regression fails in all 48 sampled radius/mode/detail combinations before the fix. Fresh game captures with the user's `my_entities1` atlas cover 100% in modes 0/1/2 and 77% in mode 2 on OpenGL 1.5, plus Vulkan at 100%. This exercises the unbuffered and buffered texture paths. Three tangent rectangles in the 100% combined screenshot change from two or three disconnected white-outline components to one 4-connected component each. These captures use a local fixture, not the live Felian server. View `build-rounding/through-rounding-check.html` for the unedited screenshots. The temporary auto-capture hook is removed from the final source and executable.

### Integration with current BestClient main

Merged upstream main `f7295c203cb80a8b1e760c5c7332269be1ed8aae` on 2026-09-28. The renderer uses the current screen rectangle, layer initialization callback, vertex color and FPS fog APIs. The updated bundled libraries and all 38 Vulkan shaders build with the client and server in Release mode.

The full test runner passes 407 tests, with three existing tests disabled. The 36 entity rounding tests include four boundary-index regressions: empty views, exact neighborhood masks, bounded traversal at extreme zoom, and equivalent coverage outside map borders.

Fresh game captures verify outer-only, inner-only and combined rounding at 100%, combined rounding at 77%, and runtime changes through 0%, 50% and 77%. OpenGL 1.5, OpenGL 3.3 and Vulkan render the through-hook fixture successfully. The three sampled tangent rectangles have one connected white-outline component each at 100% and 77%. Teleporter labels keep the same size at the ends and in the middle of the L-shaped region. Felian was also checked at close and extreme far zoom. Temporary screenshot and performance instrumentation is removed from the production source.

On Felian with Vulkan, a 1600-by-1000 viewport covering 1546-by-966 tiles, 100% combined rounding and all outlines enabled, steady samples improved from 59–86 FPS to 250–263 FPS. Outline CPU time fell from 9–14 ms to 1.1–1.2 ms, and visited cells fell from 1,493,436 to 8,953. These are local measurements on a GTX 1650 SUPER, not a guarantee for other hardware. Rounding disabled reached 490–569 FPS, but its existing central outline culling cap renders a smaller area, so that result is not an equivalent-coverage comparison.

Unedited captures: [through-hook junctions at 100%](images/entity-rounding-through.png), [fractional radius at 77%](images/entity-rounding-fractional.png), [consistent teleporter labels](images/entity-rounding-labels.png), and [Felian at extreme far zoom](images/entity-rounding-far.png).
