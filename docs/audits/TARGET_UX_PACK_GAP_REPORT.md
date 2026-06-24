# Target UX Pack Gap Report

Batch 3 records final desired UX pack IDs for every target, but this repository
currently has only these executable pack IDs:

- `compatibility`
- `cardputer_compact`
- `t_display_p4_touch`
- `uconsole_desktop`
- `tiny_node_status`
- `simulator_full`

The table below prevents missing final packs from being mistaken for completed
runtime implementations.

| target | desired ux_pack | current actual fallback | exit condition | owner |
| --- | --- | --- | --- | --- |
| `tab5` | `tab5_touch` | `compatibility` | executable `tab5_touch` pack registered and covered by app shell smoke | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `t_display_p4_tft` | `t_display_p4_touch` | `t_display_p4_touch` | closed; executable pack and page manifest are registered | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `t_display_p4_amoled` | `t_display_p4_touch` | `t_display_p4_touch` | closed; executable pack and page manifest are registered | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tlora_pager` | `pager_compact` | `compatibility` | executable `pager_compact` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tdeck` | `deck_full` | `compatibility` | executable `deck_full` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `twatch` | `watch_compact` | `compatibility` | executable `watch_compact` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `uconsole` | `uconsole_desktop` | `uconsole_desktop` | none; current pack already resolves | `apps/linux_uconsole_gtk + modules/ui_gtk_runtime` |
| `cardputerzero` | `cardputer_compact` | `cardputer_compact` | none for UX pack selection; real framebuffer, evdev, launch, and packaging validation remain tracked in the target matrix | `apps/linux_cardputer_zero + modules/ui_lvgl_ux_packs` |
| `gat562_mesh_evb_pro` | `node_headless` | `tiny_node_status` | headless descriptor consumer becomes the active nRF52 node path | `apps/nrf52_node + modules/ui_headless_runtime` |

## Rule

`TargetProfile.ux_pack_id` is final intent. `TargetUxBinding.active_ux_pack_id`
is the currently resolvable runtime pack. App shells must use the binding rather
than choose a fallback locally.
