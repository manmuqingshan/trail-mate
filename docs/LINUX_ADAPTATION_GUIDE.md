# Linux Adaptation Status Assessment and Development Guide

Evaluation date: 2026-05-08

Scope of application: This article evaluates and guides the Linux line in the Trail Mate repository, focusing on the relationship between
`apps/linux_sim`、`apps/linux_rpi`、`apps/linux_unoq`、`platform/linux/*`、
`modules/*` and `modules/core_sys/include/platform/ui/*`.

This article is not a to-do list for a single feature. Its goal is to answer three questions:

1. To what extent is the current Linux adaptation achieved?
2. What should be done first and what should be done last.
3. How to adapt to Linux gracefully instead of turning Linux into another long-term fork.

## 0. Summary

The current Linux adaptation has advanced from "the structure is formed, the simulator is verifiable, and the real device has not yet closed the loop" to the stage of "the real machine closed-loop structure is being built, but the current workspace has not yet been re-build-closed".

More specifically:

- The repository structure has promoted Linux as a first-class target, rather than a temporary experimental directory.
 - `apps/linux_sim` is still the fastest shared UI verification shell, and the source list has started to be closed via `cmake/TrailMateLinuxSources.cmake`.
 - `platform/linux/common` has implemented a number of Linux-safe platform contracts, including settings, time, screen, device, GPS NMEA/env, hostlink TCP, route/tracker file storage, pack repository, simulation runtime for SSTV/walkie/LoRa, and explicitly unsupported stubs such as Wi-Fi/USB/FOTA. This round has added structures such as runtime path, env helper, capability status, runtime mode, and demo world extraction. In the latest advancement, `CapabilityStatus` has been moved up to the `platform::ui` contract seed and entered the LoRa/SSTV/Walkie public header. Hostlink/team/SSTV/map tiles have also begun to reuse `runtime_paths` more.
- `apps/linux_rpi` still has two device paths: one for the repo-local CMake framebuffer shell and one for the `M5Stack_Linux_Libs` SCons SDK shell. The custom framebuffer path has been connected to the evdev source file and input drain, but it has not yet completed build-closed and real-machine verification; the SDK path has been advanced from a thin placeholder main to a bring-up shell with fbdev automatic detection, LVGL evdev initialization and startup logs, but it has not yet been connected to the Trail Mate shared shell.
- Bounds check exists and passes. Currently `modules/ui_shared` and `modules/core_*` do not hit key platform pollution rules such as `Arduino.h`, `Preferences`, `freertos/*`, `platform/esp/*`.
- The completion degree of the real machine is still lower than the completion degree of the simulator. Display, input, capability model, real hardware runtime, SDK main path, installation and release, and device CI have not yet formed a complete closed loop; among them, the capability model already has contract seeds and some public APIs, but has not yet entered the contract inventory and UI presentation.
- This round of code has obvious architectural progress, and the main compile/runtime gate listed in the previous round has been basically repaired; the current biggest risk has changed from "obviously inconsistent source code" to "has not yet been fully built, tested and verified on a real machine". You cannot directly mark P1/P2/P3/P4/P5/P6 as completed just because the structure file has appeared.

Judgment in one sentence:

> It is not "Linux has not started yet", nor is it "Linux has been adapted". After this round of return, the Linux line has entered the L4 structure construction stage; the structure of the input path, SDK bring-up, path security and capability contracts has significantly improved, and some of the previous round P0 items have been downgraded from "to be repaired" to "to be verified". However, the current workspace still needs to complete the remaining compilation verification and real machine verification before advancing L4 from "structural existence" to "buildable, runnable, and verifiable".

### 0.1 2026-05-07 Regression Conclusion (Historical Record)

This regression is based on the re-evaluation of code changes after `LINUX_ADAPTATION_GUIDE.md`. The conclusions are as follows:

Note: This section retains the judgment of the day of 2026-05-07 for observing the evolution; the current status is as follows: Section `0.6` of 2026-05-08 shall prevail.

| Guide items | This round of code changes | Current judgment |
| --- | --- | --- |
| P1 CMake source list closed | Added `cmake/TrailMateLinuxSources.cmake`, CMake of `linux_sim`/`linux_rpi` is obviously thinner | The direction is correct, and the structure is basically implemented; but it must be built through two sets of sim/rpi before it can be marked done |
| P2 LVGL ownership split | Added `ShellSession`, `CanvasLvglHost`, `NativeLvglHost` | The direction is correct, and it has entered the "dual host" structure from "single runner"; but the current header/callback details will still block or destroy the input |
| P3 real machine input | Added `EvdevInput`, `LinuxFramebufferPlatform::drainInput()` changed from evdev Read | Historical judgment: The loop has not been closed that day; the source wiring / key map / directory detection problem of this line has been updated in the 2026-05-08 regression |
| P4 runtime path/env | Added `platform/linux/runtime_paths.*`, `env_config.*`, route/tracker deletion path began to be limited to the root directory | Obvious progress; but settings/sstv/hostlink/team, etc. have not been completely unified into the same set of path helper |
| P5 capability truth model | Added `CapabilityState`/`CapabilityStatus`, LoRa/Walkie/SSTV began to return `Simulated` | The concept has been implemented into code, but it has not yet become part of the `platform::ui::*` contract, nor has it driven UI rendering |
| P6 demo world/facade split | Added `linux_demo_world.*` | Just the first step: the old loopback mesh, dummy crypto, demo seeding are still retained in the app facade, and `runtime_mode` has not yet truly controlled composition |
| P7 M5 SDK main path | Haven't seen shared shell access to `M5Stack_Linux_Libs` yet | Not yet completed |
| P8 first verification slice | path safety smoke is added, Settings slice has not yet formed a real experimental closed loop | The test direction is correct, but the current smoke test itself still needs include correction |

The current highest priority is not to continue to expand the functionality, but to first bring this round of structural changes back to the state of being able to build, run smoke, and explain the true and false capabilities.

### 0.2 Specific blocking points that must be repaired first in this round

These problems belong to the level of "fix them first, otherwise subsequent evaluation will be distorted":

| Type | File | 2026-05-08 Status | Action still required |
| --- | --- | --- | --- |
| Build gate | `platform/linux/common/include/app/linux_demo_world.h` / `platform/linux/common/src/app/linux_demo_world.cpp` | Last round header `<memory>`, constexpr node id, duplicate definition risk have been fixed | Confirm with a complete build; clean up the loopback mesh logic that still exists in the facade later. |
| Architecture gate | `platform/linux/common/src/app/linux_app_facade.cpp` | demo seeding has been `demo_world_enabled(resolve_runtime_mode())` gate | Still need to separate loopback mesh, dummy crypto, loopback pairing from the default composition of real devices |
| Build gate | `platform/linux/common/include/ui/shell_ui_runner.h` / `platform/linux/common/src/ui/shell_ui_runner.cpp` | callback has been restored to `lv_area_t` / `lv_indev_data_t` exact signature, header only does LVGL type forward declaration | Use full build to confirm whether LVGL typedef is compatible with forward declaration; if LVGL's `lv_area_t` / `lv_indev_data_t` is not a typedef with `_lv_*` tag, it should contain `lvgl.h` instead or drop the callback to `.cpp` private free function |
| Runtime gate | `platform/linux/common/src/ui/shell_ui_runner.cpp` | `hasPendingKeyEvent()` has been added, `continue_reading` will no longer consume the next event | You need to use a small test or real machine hand test to confirm that press/release can be received by LVGL |
 | Build gate | `apps/linux_rpi/CMakeLists.txt` | `evdev_input.cpp` has been added to device executable | Confirm with Linux/WSL CI build |
| Build gate | `platform/linux/rpi/src/platform/device/evdev_input.cpp` | The key map has been changed to `std::to_array<KeyMapping>({...})`, the number of handwriting and CTAD risks have been fixed | Confirmed with Linux build; subsequent emulator key sampling and mapping test |
| Runtime gate | `platform/linux/rpi/src/platform/device/evdev_input.cpp` | by-path/by-id does not exist. `error_code` and `is_directory` are used for protection | Real machine sampling is required. Cardputer Zero built-in keyboard event code, perfect Fn/combination key mapping |
| Build gate | `apps/linux_sim/tests/path_safety_smoke.cpp` | Already included `platform/linux/capability_status.h` | Use `ctest` to confirm that smoke actually passes |
| Contract gate | `modules/core_sys/include/platform/ui/capability_status.h` / `modules/core_sys/include/platform/ui/{lora,sstv,walkie}_runtime.h` | `CapabilityStatus` has been moved from Linux helper to `platform::ui` contract seed, Linux helper only does re-export | Also declare `capability_status()` in each runtime public header, update contract inventory, and let the UI use status instead of just reading `is_supported()` |
| Path gate | `platform/linux/common/src/platform/ui/*` / `platform/linux/common/src/ui/widgets/map/map_tiles.cpp` | hostlink, team store, SSTV, map tiles have been changed to `runtime_paths`, hostlink default bind has been changed to loopback | `pack_repository_runtime.cpp` still has ad hoc root; the old env constants in map tiles, etc. have no practical use; `runtime_paths.h` still mentions fsync, but the `.cpp` implementation has been made clear to no-fsync temp+rename, which needs to be calibrated |
| Runtime mode | `apps/linux_sim/src/targets/simulator_main.cpp` / `platform/linux/common/include/platform/linux/runtime_mode.h` | simulator main is only written to `demo` by default when the user does not set `TRAIL_MATE_RUNTIME_MODE`; Windows `_putenv_s()` has entered the same guard | Continue to start smoke with Windows/WSL to confirm env override; continue to confirm that the device shell does not enter the demo by default; the real device composition still needs to dismantle the dummy/loopback default implementation |
| Specification gate | `apps/linux_rpi/docs/specification/project-baseline.md`, etc. | Some text has been updated, but still needs to be aligned with the build/test facts | `DONE` is only used for facts verified by build/test/real machines; for target state `TARGET`, `PARTIAL` or `NOT YET VALIDATED` |

### 0.3 This round of verification records

This round of regression has done limited verification, and the conclusions should be clear:

| Command | Result | Explanation |
| --- | --- | --- |
| `python scripts/check_platform_ui_boundaries.py` | Pass | Description shared/core's ESP/Arduino/FreeRTOS include pollution boundaries are still clean |
| `cmake -S apps/linux_sim -B .codex-build/linux-sim-review -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug` | Configuration passed | Description new CMake helper Can be consumed by at least configuration phase |
| `cmake --build .codex-build/linux-sim-review --target trailate_cardputer_zero_path_safety_smoke -j 8` | Failed to verify project code | Native `clang++ 14` was blocked by VS 2022 STL with "expected Clang 19.0.0 or newer" Interception, the failure occurs in the standard library compatibility layer, it does not mean that the project code has or has not been compiled |
| `cmake -S apps/linux_sim -B .codex-build/linux-sim-msvc -G "Visual Studio 17 2022" -A x64 -DBUILD_TESTING=ON` | Timeout | Configuration not completed within 124 seconds cannot be regarded as a pass or failure conclusion |

Therefore, the compile gate in this article Listings come from code reading and target dependency regression; ultimately still to be confirmed with a full build of Linux/WSL CI or MSVC generator where available.

### 0.4 2026-05-08 Regression Conclusion

Note: This section records the previous round of 2026-05-08 regression points; the latest judgment is based on `0.6`.

The regression results of this round relative to 2026-05-07 are as follows:

| Guidelines | 2026-05-08 Observations | Current Judgments |
| --- | --- | --- |
| P1 CMake source list closure | rpi target has been explicitly added to `platform/linux/rpi/src/platform/device/evdev_input.cpp`, sim/rpi still organizes common/ui sources through shared helper | P1 continues to improve; the remaining is full build verification, helper annotation/coding cleanup, and target define semantic closure |
| P2 LVGL ownership Split | `ShellSession::hasPendingKeyEvent()` has been added, `readInputCb()` no longer consumes the next event for `continue_reading`; callback has been restored to `lv_area_t` / `lv_indev_data_t` precise signature | The previous round of input swallowing event issues has been fixed; callback risk has been narrowed to LVGL typedef/forward declaration compatibility, still requires build confirmation; SDK native The host is not yet connected |
| P3 real machine input | `evdev_input.cpp` has been compiled into rpi CMake; `/dev/input/by-path` and `/dev/input/by-id` have been protected by `error_code` and directory judgment; `kKeyMap` has been changed to `std::to_array<KeyMapping>` | source wiring, directory detection, key map The initialization risk has been fixed; the remaining is Linux build confirmation and Cardputer Zero real machine key sampling |
| `runtime_paths` |
| P5 capability truth model | `CapabilityState` is still a Linux internal helper, LoRa/Walkie/SSTV is still marked through `capability_status()` `Simulated` | Concept is stable, but has not yet entered the `platform::ui::*` public contract and UI rendering |
| P6 demo world/facade Split | `runtime_mode.h` added `<cstring>`, `ensureStarted()` used `demo_world_enabled(resolve_runtime_mode())` gate demo seeding, simulator main explicitly defaults to `TRAIL_MATE_RUNTIME_MODE=demo` | demo seed default semantics are clearer; but facade still always combines loopback mesh, dummy crypto, loopback pairing, DeviceLocal is not yet a clean real device composition |
| P7 M5 SDK main path | Not seen `apps/linux_rpi/main/src/main.cpp` Access `ShellSession`/`NativeLvglHost` | Not yet completed |
| P8 first verification slice | path safety smoke The source code is more complete, but the local effective build/ctest has not been completed | The test direction is correct, acceptance still relies on Linux/WSL/CI build |

In one sentence: This round has basically fixed a batch of "clear red lights" on 2026-05-07 to "waiting for build confirmation", but has not yet crossed the three gates of build/test/real machine. The next minimal closed-loop step should be to build `linux_sim` tests and `linux_rpi` framebuffer target directly on Linux/WSL, and then do Cardputer Zero real machine key sampling and navigation acceptance.

### 0.5 2026-05-08 Latest regression conclusion

This round continues to advance three types of structures after `0.4`: SDK bring-up, capability contract moving up, and runtime path coverage. The conclusion is more detailed than `0.4`: some items have changed from "missing structure" to "missing public API/missing validation", but still cannot be directly marked as complete.

| Guide items | Latest observations | Current judgment |
| --- | --- | --- |
| P1 CMake source list closed | The path comment of `cmake/TrailMateLinuxSources.cmake` has been calibrated to "push repo root from the directory where the helper is located"; rpi target continues to be explicitly compiled into `evdev_input.cpp` | The structure continues to be healthy; the current remaining is no longer abstract, but a complete Linux/WSL build, ctest and rpi target build |
| P2 LVGL ownership split | `ShellSession` / `CanvasLvglHost` / `NativeLvglHost` structures are no longer regressed; precise callback signatures and `hasPendingKeyEvent()` remain | direction stable; still need to build to confirm LVGL typedef/forward declaration compatibility, and switch SDK main loop from annotation to `NativeLvglHost` |
| P3 real machine input | CMake framebuffer path's evdev adapter is still the current shared shell input mainline; SDK path has also been created with LVGL evdev pointer/keypad | The input structure is significantly enhanced; real machine event code sampling, Fn/key combination mapping, press/release regression and rpi build results are still missing |
| P4 runtime path/env | Settings, route, tracker, hostlink, team store, and SSTV have basically adopted `runtime_paths`; the default hostlink bind is `127.0.0.1`. When external monitoring is required, explicitly set `TRAIL_MATE_HOSTLINK_BIND=0.1.30-alpha.0` | P4 has advanced from "scattered helper" to "most core write point closure"; the remaining map tiles, pack repository, old env constant, `safe_write_under_root()` fsync annotation/implementation inconsistency |
| P5 capability truth model | Added `modules/core_sys/include/platform/ui/capability_status.h`, Linux side `platform/linux/capability_status.h` becomes re-export; LoRa/Walkie/SSTV implementation layer returns `Simulated` | The concept has entered the contract layer, but the public runtime headers have not declared `capability_status()`, the contract README has not been included in `capability_status.h`, and the UI has not been consumed yet; therefore it can only be counted as "contract seed landing", not the completion of capability presentation |
| P6 demo world/facade split | `runtime_mode` continues to only gate demo seed; `MinimalLinuxAppFacade` Still always combine loopback mesh, dummy crypto, loopback pairing | The data seed boundary of demo/local is clearer, but the real device composition is still not clean; there is also a small pit that Windows simulator will overwrite the user's default `TRAIL_MATE_RUNTIME_MODE` |
| P7 M5 SDK main path | `apps/linux_rpi/main/src/main.cpp` already has ST7789 framebuffer detection, LVGL fbdev, LVGL evdev pointer/keypad, boot logs and bring-up UI; shared shell access code is still being commented | SDK path is no longer an empty shell, but is still a bring-up UI, not a Trail Mate shared shell; L5 is not yet complete |
| P8 first verification slice | No valid build/ctest results have been added in this round; local builds have been intercepted by Windows clang/MSVC STL version | Continue to maintain "code reading evaluation + boundary check verifiable", the final conclusion still has to wait for Linux/WSL CI or real machine acceptance |

In a word: The latest advancement has raised the structural maturity of P4/P5/P7 to another level, but Linux The line is still stuck at the stage where "the structure is becoming more and more like its final form, and the verification has not yet closed the loop." The next priority is not to continue adding functions, but to run through these structures: Linux/WSL build, capability public API, SDK shared shell access, and real machine input sampling.

### 0.6 2026-05-08 Returning to the conclusion again

 Compared with `0.5`, this round has advanced three gaps named in the previous round: simulator runtime mode coverage semantics, LoRa/SSTV/Walkie capability public API, and map tile runtime path. The conclusion is as follows:

| Guide items | Latest observations | Current judgment |
| --- | --- | --- |
| P0-E runtime mode override semantics | `apps/linux_sim/src/targets/simulator_main.cpp` now only writes `demo` when `TRAIL_MATE_RUNTIME_MODE` is not set; Windows `_putenv_s()` is also placed behind the same guard | The Windows overwriting user default issue pointed out in the previous round has been fixed; the remaining is to start smoke with Windows/WSL Verify env override |
| P4 runtime path/env | `platform/linux/common/src/ui/widgets/map/map_tiles.cpp` has been changed to `resolve_paths().sd_root`; hostlink/team/SSTV/map tiles will no longer copy the complete storage root fallback | P4 continues to get better; still remaining `pack_repository_runtime.cpp` pushes the repo root from `__FILE__` by default, the old `kSdRootEnv`/`kSettingsRootEnv` constants in map tiles are no longer useful, the `runtime_paths.h` header file still writes fsync but the implementation has been changed to no-fsync temp+rename |
| P5 capability truth model | `lora_runtime.h`, `sstv_runtime.h`, `walkie_runtime.h` have included `platform/ui/capability_status.h` and publicly declared `CapabilityStatus capability_status()` | P5 has advanced from "contract seed implementation" to "partial runtime public API implementation"; the rest is to update `modules/core_sys/include/platform/ui/README.md` contract contract inventory, and let the UI consume this status |
| P7 M5 SDK main path | `apps/linux_rpi/main/src/main.cpp` is still bring-up UI, and shared shell access is still stopped at comments | This round has not been advanced; L5 has not yet been completed |
| P8 verification slice | The boundary check was re-run this round and passed; it is still not completed Linux/WSL Build, ctest or real machine verification | The boundaries are clean, but the build/run gate is still a hard threshold for the next step |

In a word: This round fills in several "structural gaps" in `0.5`, especially the P0-E and P5 public API. The most worthwhile thing to do right now is not to continue writing more runtimes, but to clean up the two remaining documentation/API holes, and then run Linux/WSL builds and at least one capability UI consumption closed loop.

## 1. Let's make a distinction first

### 1.1 The concepts that are most easily confused at present

Linux adaptation cannot just be understood as "enabling code to be compiled on Linux". There are at least these objects in the current repository that must be separated:

| Concept | What it is | What it is not |
| --- | --- | --- |
| Linux target line | Trail Mate's first-class platform family | Not another board profile for ESP/Arduino |
| `apps/linux_sim` | Desktop simulator and development tool shell | Not a real machine runtime and should not be inherited from the Pi OS hardware details |
| `apps/linux_rpi` | Pi OS / Cardputer Zero real app shell | Should not absorb simulator geometry, desktop input or demo app structure |
| `platform/linux/common` | Linux-safe implementation layer shared by both simulator and rpi | Should not put SDL shell, fbdev device assumptions or business rules |
| `platform/linux/rpi` | Pi OS / framebuffer / Device Adaptation Layer | Shared UI page logic should not be placed |
| `M5Stack_Linux_Libs` | Real device SDK baseline | Not a source of Trail Mate architectural boundaries |
| `M5CardputerZero-UserDemo` | Board level clue reference | Not a project template |
| Platform contract | Collaboration surface in `modules/core_sys/include/platform/ui/*` | Not an implementation detail of a certain platform |
| Platform implementation | Concrete implementation in `platform/esp/*`, `platform/linux/*` | Does not own domain/usecase/UI structure |
| Capability support | Real or interpreted runtime capabilities | Not "has an entry on the menu" or "has a mock stub" |

### 1.2 Current specification Baseline

Subsequent Linux adaptation should adhere to the following baseline:

- `modules/core_*` is the core business, protocol, policy, use case, state and pure tooling layer.
- `modules/ui_shared` is a shared LVGL presentation layer, which can rely on core modules and platform contracts, but cannot rely on platform implementation.
- `modules/core_sys/include/platform/ui/*` is the contract layer between the shared UI and the platform implementation.
- `platform/linux/common` implements the Linux shared contract and only puts things where both simulator and device are established.
- `platform/linux/rpi` implements Pi OS / Cardputer Zero specific adaptation.
- `apps/linux_sim` and `apps/linux_rpi` only do composition root, launch and target selection.
 - `M5Stack_Linux_Libs` should be located under the Trail Mate platform boundary and consumed by the Linux device shell or `platform/linux/rpi`.

### 1.3 Clarify illegal cutting methods

These practices will make Linux adaptation inelegant and should be avoided in the future:

- Treat `Cardputer Zero` as another MCU board and force abstract access through `BoardBase`.
- Write SDL, fbdev, evdev, `/dev/input/*`, `/dev/fb*` into `ui_shared`.
- Copy the directory structure or app organization of `M5CardputerZero-UserDemo` into the warehouse.
- Mistake "environment variables can be simulated" as "real machine capabilities are supported".
- Treat the simulated data of hostlink, GPS, SSTV, and LoRa as the real capabilities of the product.
- Because both Linux apps in CMake require the same batch of source codes, I hand-write two source lists for a long time.
- Sprinkle `#ifdef __linux__` or `#ifdef _WIN32` in shared/core to escape boundary design.
- Directly stack real machine drivers, UI pages and business logic in `apps/linux_rpi`.

## 2. Current adaptation level

### 2.1 Maturity level

The level here is not test coverage, but the maturity of architecture and product usability.

| Level | Meaning | Current status |
| --- | --- | --- |
| L0 | There are directories and documents, Linux is recognized as the target line | Completed |
| L1 | Linux host can configure and compile basic targets | Historically established; build gate still needs to be re-run after this round of structural changes |
| L2 | The desktop simulator can run a shared shell, forming a fast verification closed loop | The structure is established; still needs to be re-proven simulator build/test |
| L3 | Linux common runtime covers the main platform contracts, there are smoke tests | Basically completed and there is path/capability progress in this round; `CapabilityStatus` has been moved up to the contract seed and entered the LoRa/SSTV/Walkie public header, the path smoke source code has been corrected but not accepted |
| L4 | Pi OS The framebuffer/device shell can compile and start the shared UI | The structure has been significantly improved, evdev source wiring has been completed; the SDK bring-up shell is more complete; it has not yet been built/run-closed |
| L5 | `M5Stack_Linux_Libs` main path runs Trail Mate shared shell | SDK bring-up has been enhanced, but the shared shell has not yet been connected |
| L6 | Real machine display, input, storage, GPS, audio, network/wireless capabilities closed loop according to capability model | Not completed |
| L7 | Installable, releasable, regressable, can support multiple Linux Device Family | Not Completed |

The current overall conclusion: The direction and code surface of L3 are established, but it still must be re-build-closed after this round of changes; L4 has moved from display-only to the structural stage of display+input + SDK bring-up, but it is still in an unaccepted state; L5 has not yet closed the loop.

### 2.2 Sub-evaluation

| Dimension | Current extent | Evidence | Gap |
| --- | --- | --- | --- |
| Directory structure | Better | `apps/linux_sim`, `apps/linux_rpi`, `apps/linux_unoq`, `platform/linux/common`, `platform/linux/rpi` already exist; `cmake/TrailMateLinuxSources.cmake` added in this round | `platform/linux/unoq` only has planning; Linux docs The hierarchical relationship with app-local specs still needs to be maintained |
| Architecture specifications | Better | `docs/LINUX_ADAPTATION_GUIDE.md` and `apps/linux_rpi/docs/specification/*` have formed a final-shape / checklist / regression guide combination | Individual `DONE` status in app-local specs is more optimistic than the code, and needs to be calibrated with build/test results |
| Boundary protection | Better | `scripts/check_platform_ui_boundaries.py` exists and protects shared/core from being polluted by ESP | The rules are also biased toward include pollution, and have not yet covered CMake/source ownership, capability semantics, path safety, demo/real composition |
| Simulator | Better | `apps/linux_sim` has SDL3 simulator, CMake presets, tests, WSL/dev-container scripts, and start reusing shared CMake helper | After this round of changes, simulator build/test must be re-certified; the boundary between simulator capabilities and real capabilities needs to be more clearly marked |
| Linux common runtime | Medium preference | settings/time/screen/device/GPS/hostlink/tracker/route/SSTV/walkie/LoRa/pack repository has been implemented; the basics of runtime path/env/capability continue to advance, and hostlink/team/SSTV/map tiles have begun to be closed to `runtime_paths` | Many are soft runtime or synthetic runtime and should not directly claim real machine support; pack repository still has ad hoc path root; capability has not yet entered the UI |
| Pi OS CMake device shell | Medium to early | custom framebuffer target uses shared shell runner, the real machine input has been drained from `EvdevInput`, rpi CMake has been compiled into the evdev source file, and the key map has been changed to `std::to_array<KeyMapping>` | Still needs Linux build confirmation; Cardputer Zero real machine key verification is still not closed |
| M5Stack SDK path | Early stage | `apps/linux_rpi/SConstruct`, `main/SConstruct`, `config_defaults.mk`, `main/src/main.cpp` already exist; main already has ST7789 framebuffer detection, LVGL fbdev, LVGL evdev pointer/keypad and startup log | SDK path is still bring-up UI, shared shell Access is only in comments, not reused Trail Mate `ShellSession` / `NativeLvglHost` |
| UI sharing | Medium preference | `trailmate_cardputer_zero_ui_shell` compiles a large number of `modules/ui_shared` pages and assets; added `ShellSession`/`CanvasLvglHost`/`NativeLvglHost`, input event swallowing problem has been fixed, callback has returned to LVGL precise type signature | SDK native path has not yet been connected; still needs to be fully built to confirm LVGL typedef/forward declaration compatibility |
| CMake structure | Significant improvements | linux_sim and linux_rpi have been changed to include shared helpers, source lists are no longer mainly scattered in the two app CMake, rpi-only evdev source has been connected, and helper path annotations have been calibrated | Need to run sim/rpi build; target compile definitions and runtime mode default semantics need to be tightened |
| Real machine capabilities | Early stage | fbdev path, LVGL evdev bring-up, NMEA serial path, hostlink TCP path have taken shape; hostlink default loopback is more secure | Cardputer Zero key position, battery, brightness, Wi-Fi, USB, firmware update, real radio/audio, etc. are not closed loops |
| CI | Medium preference | `.github/workflows/linux-simulator.yml` covers simulator build/tests; `.github/workflows/uconsole-linux.yml` covers uConsole GTK build/tests/package | Cardputer Zero Linux no longer borrows this workflow name; still does not build M5 SDK path, does not run real machine/virtual fbdev, does not verify Docker GUI path |
| Development experience | Good | Windows PowerShell, Linux shell, WSL validate, Docker dev container scripts are complete | Local environment variables and path bridge are still evolving; path/permission policies of different hosts need to be documented uniformly |

### 2.3 What has been done well

1. Linux is not a plug-in directory, but enters the target structure.

`apps/README.md`, `docs/ARCHITECTURE.md`, `platform/linux/README.md` all indicate that the Linux target line has been incorporated into the long-term structure.

2. The boundaries between Simulator and real device have begun to separate.

The README of `apps/linux_sim` specifies simulator-first, and the README of `apps/linux_rpi` specifies Pi OS device shell. `platform/linux/common` is defined as a shared layer between both.

3. Shared UI can already be consumed by Linux shell.

Linux CMake target compiles a large number of `modules/ui_shared/src/ui/*` pages, components, assets, and enters the shared boot/menu shell through `SharedUiShellStartup`.

4. Core module platform pollution has been significantly improved.

The current boundary check is passed, `modules/ui_shared/library.json` no longer has the ESP Arduino include root. Certain violations mentioned in past specifications have been fixed.

5. Linux common runtime is no longer an empty shell.

`platform/linux/common/src/platform/ui/*` has covered a number of real or simulated capabilities: file settings, screen timeout, GPS NMEA, hostlink TCP, tracker/route files, team UI store, pack repository, SSTV/walkie/LoRa synthetic runtime, etc.

6. CMake duplication has begun to close.

`cmake/TrailMateLinuxSources.cmake` brings common sources, UI shell sources, include roots and warnings helper to the shared layer. When adding shared UI files later, simulator and rpi should no longer each maintain a large source list.

7. The right direction of LVGL ownership has emerged.

`ShellSession` initially only has app facade, startup, event queue and per-frame app tick; `CanvasLvglHost` has `lv_init()`, display, RGB565 buffer and canvas copy; `NativeLvglHost` leaves an entrance for the SDK-owned LVGL path. This is the correct intermediate form of accessing `M5Stack_Linux_Libs`.

8. Real machine input and path security have begun to change from "verbal requirements" to codes.

`EvdevInput` has put `/dev/input/event*`, by-path/by-id keyboard detection, `TRAIL_MATE_INPUT_DEVICE` override, and the mapping of Linux key code to `InputEvent` to `platform/linux/rpi`. `runtime_paths` and `resolve_child_under_root()` also allow route/tracker deletion to start to have root containment; the latest advancement also closes hostlink, team store, SSTV and other write points to the same set of runtime roots.

9. CI has been able to block basic regression.

The dedicated `Linux Simulator` workflow will run simulator build and smoke tests; the `uConsole Linux` workflow will run uConsole GTK build, smoke tests and Debian package. Cardputer Zero Linux requires a separate device-shell / hardware validation gate, which is currently no longer implied by the workflow name.

10. The authenticity of capabilities begins to move from Linux private concepts to public contracts.

`modules/core_sys/include/platform/ui/capability_status.h` has become a shared type source for `Unsupported` / `Simulated` / `Available` / `Degraded` / `Error`, and the Linux side `platform/linux/capability_status.h` has degenerated into re-export. The public header of LoRa/SSTV/Walkie has exposed `capability_status()`, which is a good direction; the next step is to update the contract inventory and let the UI actually consume these states instead of just staying at the implementation layer or document layer.

### 2.4 Main unfinished items

1. The main path of the real machine has not yet closed the loop.

There are currently two device paths:

- CMake custom framebuffer path: The shared shell is connected, and the evdev input source file has been connected to the rpi target; but it has not yet passed the Linux build and real machine operation acceptance.
- `M5Stack_Linux_Libs` SCons path: closer to the future real machine main path, currently has fbdev automatic detection, LVGL evdev, startup log and bring-up UI, but has not yet run the Trail Mate shared shell.

These two paths need to converge: give priority to the SDK path to run the shared shell, while retaining the custom framebuffer path as a lightweight fallback or verification tool.

2. The prototype of LVGL ownership has been taken out, but it has not yet been accepted.

The current division of responsibilities of `ShellSession`, `CanvasLvglHost`, and `NativeLvglHost` is much more correct than the previous version. The new issues are:

- `shell_ui_runner.h` has forward-declared `lv_area_t`, `lv_indev_data_t`, callback has been reverted to LVGL exact type signature. Subsequent construction must confirm whether these forward declarations are compatible with LVGL's own typedefs; if LVGL uses anonymous typedefs, the safest approach is to change the callback to a `.cpp` private free function or directly include `lvgl.h` in the header.
- The event swallowing problem of `readInputCb()` has been fixed through `hasPendingKeyEvent()`, but it needs to be confirmed by testing or real machine input regression.
- `NativeLvglHost` is just a thin host and has not been consumed by the `M5Stack_Linux_Libs` main loop.

3. The input has started to adapt to the device, but there are blocking points.

The SDL emulator has full keyboard and mouse mapping. Pi CMake framebuffer path now returns `InputEvent` via `EvdevInput`, which is key development. The previous round of source wiring and directory detection problems have been basically fixed. The remaining must be repaired:

- `kKeyMap` of `EvdevInput` has been changed to `std::to_array<KeyMapping>({...})`, the number of handwriting and CTAD risks have been eliminated; the follow-up focus is on Linux build confirmation and real machine key sampling.
- The Fn/key combinations/direction keys of Cardputer Zero's built-in keyboard need to be sampled from real machine event codes to establish a mapping table. Don't just rely on PC keyboard key code.

4. The authenticity of abilities still needs to be institutionalized.

For example:

- `lora::is_supported()` currently returns true, but the implementation is synthetic RSSI.
 - `walkie::is_supported()` currently returns true, but the implementation is synthetic volume/transmit level.
- `sstv::is_supported()` currently returns true, but the implementation is to generate PPM simulation plots.
- `wifi::is_supported()`, `usb_support::is_supported()`, `firmware_update::is_supported()` returns false, which is more honest.

The addition of `CapabilityState`/`CapabilityStatus` in this round is the right direction, and the latest code has put it into `modules/core_sys/include/platform/ui/capability_status.h`, which means that it has begun to become a platform contract. The public header of LoRa/SSTV/Walkie has declared `capability_status()`. The remaining issues are: the contract inventory of `modules/core_sys/include/platform/ui/README.md` does not yet list `capability_status.h`; the shared UI does not yet replace "see only `is_supported()`" with that status. It is necessary to institutionalize the distinction between `Unsupported`, `Simulated`, `Available`, `Degraded` and `Error` in the future, otherwise the UI and user expectations will be distorted.

5. The build description duplication has been mitigated, but the build gate has not yet passed.

`cmake/TrailMateLinuxSources.cmake` has resolved the largest chunk of source list duplication. The remaining issues are:

- The rpi-only source file already has an explicit access point. You can consider rpi adapter target in the future, but you don't have to reconstruct it immediately for the sake of form.
- The simulator main has explicitly set the default `TRAIL_MATE_RUNTIME_MODE=demo`, which is clearer than letting the common target define determine the simulator default behavior. The latest code has put the default write behind the `std::getenv("TRAIL_MATE_RUNTIME_MODE")` guard, and Windows and POSIX semantics have been aligned; later, start smoke will be used to verify that the user defaults will not be overwritten, and confirm that the device shell defaults to `local`, so that real device composition no longer uses dummy/loopback by default.

6. File path security has been improved, but not all write points have been covered.

`route_storage::remove_route()`, `tracker::remove_track()` have started to use `resolve_child_under_root()`. The latest advancement has added `runtime_paths` to the default root of hostlink, team store, SSTV output, and map tiles. The next step is to extend the same set of path helpers to path writing points such as pack repository and settings temp file, and clean up old env constants in map/hostlink/team/SSTV that have no practical use. The `.cpp` comment of `safe_write_under_root()` has been changed to no-fsync temp+rename, but the header still reads fsync; either add real fsync / `FlushFileBuffers`, or change the header comment to an accurate description.

7. The demo/loopback logic only completes the extraction of the starting point.

`linux_demo_world.*` has appeared, and demo peer seeding has passed the `runtime_mode` gate; but `MinimalLinuxAppFacade` still retains the logic that old `LinuxLoopbackMeshAdapter`, dummy crypto, loopback pairing service and other real devices should not be enabled by default. `runtime_mode` doesn't really determine the entire facade composition yet.

8. Real machine security and network defaults require continued caution.

`hostlink` is currently bound to `127.0.0.1` by default, which is more suitable for real machine security defaults than the previous version. When external monitoring is required, `TRAIL_MATE_HOSTLINK_BIND=0.1.30-alpha.0` should be explicitly set and stated in the UI or documentation that this will expose the port to the network where the device is located.

## 3. Target structure planning

### 3.1 Final directory responsibility

The recommended target structure remains as follows:

```text
trail-mate/
  apps/
 linux_sim/ # Desktop simulator, WSL, dev-container, host tooling
 linux_rpi/ # Cardputer Zero / Pi OS real machine app shell
 linux_unoq/ # Future UNO Q Linux shell

  modules/
 core_sys/ # System contract, clock, portable utilities
    core_chat/        # chat domain/usecase/protocol-neutral infra
    core_gps/         # GPS domain/filter/policy
    core_team/        # team domain/protocol/usecase
    core_hostlink/    # hostlink protocol/session
    ui_shared/        # LVGL shared UI and presentation

  platform/
    linux/
      common/         # Linux-safe implementations shared by sim and rpi
      rpi/            # Pi OS/Cardputer Zero specific adapters
      unoq/           # UNO Q specific adapters when introduced
    esp/
      ...
    shared/
      ...
```

### 3.2 Dependency direction

Legal direction:

```text
apps/linux_* -> platform/linux/*
apps/linux_* -> modules/ui_shared
apps/linux_* -> modules/core_*

modules/ui_shared -> modules/core_*
modules/ui_shared -> platform contracts in modules/core_sys/include/platform/ui/*

platform/linux/* -> platform contracts
platform/linux/* -> OS / SDK / drivers

modules/core_* -> standard C/C++ and portable module dependencies only
```

Illegal direction:

```text
modules/core_* -> platform/linux/*
modules/core_* -> platform/esp/*
modules/ui_shared -> platform/linux/*
modules/ui_shared -> platform/esp/*
platform/linux/common -> apps/linux_sim/*
platform/linux/common -> apps/linux_rpi/*
apps/linux_rpi -> apps/linux_sim/*
```

### 3.3 Suggested new structures

These structures can be added gradually in the future and do not require one-time completion:

```text
cmake/
  TrailMateLinuxSources.cmake
  TrailMateModuleTargets.cmake

modules/core_sys/include/platform/ui/
  capability_status.h

platform/linux/common/include/platform/linux/
  runtime_paths.h
  capability_status.h  # re-export only; shared contract lives in modules/core_sys
  env_config.h

platform/linux/common/src/platform/linux/
  runtime_paths.cpp
  env_config.cpp
  safe_file_ops.cpp

platform/linux/rpi/src/platform/device/
  evdev_input.cpp
  evdev_input.h
  m5stack_lvgl_shell_host.cpp
  m5stack_lvgl_shell_host.h

platform/linux/common/include/ui/
  shell_session.h

platform/linux/common/src/ui/
  shell_session.cpp
  canvas_lvgl_host.cpp
  native_lvgl_host.cpp
```

Core intention:

- CMake source ownership needs to be centralized.
- Do not repeat path/env/capability in more than a dozen runtime files.
- LVGL session should be separated from LVGL display owner.
- The real device input should become the adapter of `platform/linux/rpi` and should not be written into the shared UI.

## 4. What to do next

First look at the priorities after this round of regression. Do not directly follow the order of the previous version of the backlog:

| Priority | Matters | Why do it now | Completion standards |
| --- | --- | --- | --- |
| P0-A | Complete the construction closed loop | The main source code red light has been repaired, the next step is to confirm that common/ui/rpi/test is established with a real build | sim common, ui shell, path safety smoke, rpi framebuffer target can all be compiled |
| P0-B | Return Shell input callback | The event swallowing problem has been fixed with peek semantics, but it needs to be verified whether press/release are received by LVGL | After a single `InputEvent` enqueue, LVGL can see press and release |
| Compilation passed; only downgrade without crash when there is no input device |
| P0-D | Calibrate DONE status of docs/spec | Some DONE in app-local specs are target status, not accepted facts | DONE in the document only represents facts verified by build/test/run |
| P0-E | Verify simulator runtime mode coverage semantics | The code has been changed to only Fill in `demo` when env is not set, and Windows `_putenv_s()` has also entered guard | Both Windows and POSIX prove that user defaults will not be overwritten by starting smoke |
| P1 | Continue to close the CMake helper | The helper already exists, the next step is to make it stable, maintainable, and CI-checkable | Add shared source and only change one thing; sim/rpi CI is all green |
| P2 | Complete the LVGL host/session layering | This is the prerequisite for connecting to the SDK path | simulator/custom fbdev uses `CanvasLvglHost`, SDK path Use `NativeLvglHost` |
| P3 | Real machine input sampling and mapping | evdev adapter only solves read events, and has not proven Cardputer Zero's built-in keyboard semantics | There are event code sampling records, mapping tables, real machine navigation acceptance |
| P4 | Unified runtime path/env/capability | Basic helper has covered more runtime, map tiles have been closed; pack repository, old env constants and some comments are still not closed | All file runtimes use the same set of root/path helpers; each runtime public header can report capability status; synthetic capabilities no longer pretend to be real |
| P5 | The demo world is separated from the real facade | New files have appeared, but the composition has not changed | `SimulatorDemo`, `DeviceLocal`, `DeviceRealMesh` use clearly different wiring |
| P6 | SDK path to shared shell | SDK bring-up has been enhanced and can be switched to `NativeLvglHost` | `apps/linux_rpi/main/src/main.cpp` is no longer bring-up UI, but Trail Mate shared shell |

### P0: Align the current document with the specification

Goal: The document cannot continue to describe debts that have been repaired, nor can it write the current demo capabilities into real machine capabilities.

DO:

 - Keep `apps/linux_rpi/docs/specification/*` as historical and target-specific specification.
- Use this article as a general guide for top-level Linux adaptation.
- Update the "known current violations" that have expired in the old document. For example, the ESP include dependency of `ui_shared/library.json` is no longer established.
- Clarify the hierarchical relationship between this article and app-local specs in the README.

Acceptance:

- New colleagues can judge where the file should be placed by reading `docs/LINUX_ADAPTATION_GUIDE.md`.
- Old documents no longer mislead engineers to fix problems that no longer exist.

### P1: Eliminate duplication of Linux CMake source list

Regression status: Implementation has begun. `cmake/TrailMateLinuxSources.cmake` has assumed shared source list, include roots and target helpers, rpi-only `evdev_input.cpp` has also been explicitly connected to the device target, and the helper's repo root annotation has been calibrated. The next step is not to redesign, but to confirm the target define semantics and make both simulator/rpi builds pass.

Goal: Let `linux_sim` and `linux_rpi` share the same set of CMake source/target definitions.

Recommended approach:

1. Create a new `cmake/TrailMateLinuxSources.cmake`.
2. Extract these variables:

```cmake
set(TRAIL_MATE_LINUX_COMMON_SOURCES
    "${TRAIL_MATE_LINUX_COMMON_ROOT}/app/linux_app_facade.cpp"
    "${TRAIL_MATE_LINUX_COMMON_ROOT}/platform/ui/settings_store.cpp"
    ...
)

set(TRAIL_MATE_UI_SHARED_SOURCES
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_runtime.cpp"
    ...
)
```

3. Provide helpers:

```cmake
function(trailmate_add_linux_common target_name)
    add_library(${target_name} STATIC ${TRAIL_MATE_LINUX_COMMON_SOURCES})
    target_include_directories(${target_name} PUBLIC ...)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()

function(trailmate_add_linux_ui_shell target_name common_target)
    add_library(${target_name} STATIC ${TRAIL_MATE_UI_SHARED_SOURCES})
    target_link_libraries(${target_name} PUBLIC ${common_target} lvgl)
endfunction()
```

4. `apps/linux_sim/CMakeLists.txt` only retains simulator-specific SDL targets.
5. `apps/linux_rpi/CMakeLists.txt` only retains device-specific framebuffer targets.

Note:

- Extract the source list first, do not rush to turn all `modules/core_*` into independent CMake targets.
- Make sure CI, WSL validate, Windows VS preset can still work after extraction.

Acceptance:

- When adding a shared UI source file, you only need to change the CMake source list.
- The simulator uses the same common/ui sources as the rpi CMake target.

### P2: Split LVGL shell session and display owner

Return status: Implementation has begun. The shape of `ShellSession`, `CanvasLvglHost`, `NativeLvglHost` is correct; the input dequeue semantics have been fixed to peek, and the LVGL callback has been restored to a precise typed signature. Currently, a complete build is required to confirm LVGL typedef/forward declaration compatibility and access to the SDK main loop.

Goal: The same Trail Mate shared shell can run on SDL/canvas or on the LVGL display created by `M5Stack_Linux_Libs`.

Current issues:

- `ShellUiRunner` is responsible for `lv_init()`, display creation, buffer creation, input creation, app facade, startup, tick, canvas copy.
 - This is convenient for the emulator, but inelegant for the SDK path, since the SDK already has the fbdev/evdev/LVGL backend.

Recommended disassembly method:

```text
ShellSession
 - Bind MinimalLinuxAppFacade or future LinuxAppFacade
  - setTeamUiEventDispatcher
  - SharedUiShellStartup begin/tick
  - app facade update/tick/dispatch
 - Shared logic before and after lv_timer_handler
 - Do not create display, do not own lv_init/lv_deinit

CanvasLvglHost
 - Give to simulator/custom Use fbdev
 - owns lv_init/lv_deinit
 - creates LVGL display and RGB565 buffer
 - flushes and copies to Canvas

NativeLvglHost
 - uses M5Stack SDK path
 - does not own display
 - uses LVGL created by SDK display/input
 - Only drives ShellSession tick
```

Implementation details:

- `lv_init()` Each process can only have clear owners. Cannot be called individually by simulator, SDK, and shell session.
- `lv_deinit()` can only be called by owner.
- When `ShellSession` is destructed, it only unbinds the facade and dispatcher, but does not delete the display/input it does not own.
- `ShellSession::tick()` should not sleep. sleep is determined by the host runner.
- `CanvasLvglHost` continues to use 16 ms frame pacing.
- `NativeLvglHost` is called in the SDK main loop according to the SDK recommended tick cadence.

Acceptance:

- The simulator can still run.
- CMake custom framebuffer shell can still run.
- SDK `main/src/main.cpp` can switch from bring-up UI to Trail Mate shared shell.

### P3: Real device input adaptation

Return status: Implementation has begun. `EvdevInput` has appeared and been called by `LinuxFramebufferPlatform::drainInput()`, rpi CMake source wiring, by-path/by-id directory detection exception, and key map initialization risk have been fixed. Currently, Linux build confirmation and real machine key sampling must be completed.

Goal: Cardputer Zero can operate shared shell via built-in keyboard/buttons on Pi OS.

Current status:

- SDL simulator input complete.
- CMake framebuffer device shell has started to receive `EvdevInput`, but it has not yet passed the compilation and real machine button acceptance.
 - SDK bring-up path has `LV_LINUX_KEYBOARD_DEVICE` and evdev hint, but does not yet enter Trail Mate input semantics.

Recommended route:

1. Short term: Use LVGL evdev keypad on SDK path.

Suitable for fast real machine bring-up. `apps/linux_rpi/main/src/main.cpp` already has a similar structure:

```cpp
lv_indev_t* keyboard = lv_evdev_create(LV_INDEV_TYPE_KEYPAD, keyboard_device);
lv_indev_set_display(keyboard, display);
```

Need to add:

-Default device discovery does not just hardcode a certain `/dev/input/by-path/...`.
 - Support explicit override of `LV_LINUX_KEYBOARD_DEVICE`.
- Keep detection logs and don't be silent on failure.

2. Mid-term: Make evdev -> `InputEvent` adapter in `platform/linux/rpi`.

Suitable for custom framebuffer path and future UNOQ path:

```text
platform/linux/rpi/src/platform/device/evdev_input.{h,cpp}
```

Responsibilities:

- nonblocking open evdev file.
- Read `struct input_event`.
- Handle `EV_KEY` press/release/repeat.
 - Map Linux key code to `trailmate::cardputer_zero::app::InputEvent`.
 - Allow env override: `TRAIL_MATE_INPUT_DEVICE`.
- Support multiple candidate paths: `/dev/input/by-path/*-event-kbd`, `/dev/input/event*`.
 - Do not expose evdev header files to `platform/linux/common`.

Mapping suggestions:

| Linux key | Trail Mate key |
| --- | --- |
| `KEY_ESC` | `Power` or back, depending on device semantics |
| `KEY_ENTER` | `Enter` |
| `KEY_BACKSPACE` | `Backspace` |
| `KEY_TAB` | `Tab` |
| `KEY_HOME` | `Home` |
| `KEY_END` | `Next` |
| `KEY_LEFT/RIGHT/UP/DOWN` | directional keys |
| printable ASCII | `Character` |
| `KEY_LEFTSHIFT/RIGHTSHIFT` | `Shift` |
| `KEY_LEFTCTRL/RIGHTCTRL` | `Ctrl` |
| `KEY_LEFTALT/RIGHTALT` | `Alt` |

Implementation details:

- Input adapters should not call LVGL directly.
- `SurfacePresenter::drainInput()` can continue to host the input queue in the short term, but can tear out the `InputSource` in the long term.
- The repeat strategy should be clear: the navigation keys can be repeated, text input can be repeated by the system, and the modifier cannot be repeated.
- Character input must take shift into account. In the short term, you can use simple US keyboard map; if you need Chinese/IME in the long term, you should use `ui_shared` IME.

Acceptance:

- Real machine buttons can enter menus, open pages, return, and enter text.
- `TRAIL_MATE_INPUT_DEVICE=/dev/input/eventX` can override the device.
- When no device is entered, the device shell clears the log prompt and continues to display the UI.

### P4: Centralize Linux runtime path/env/capability

Return status: Partially implemented and this round continues to advance. `runtime_paths.*` and `env_config.*` have appeared, and the deletion path of route/tracker has begun to be root containment; settings, hostlink, team store, SSTV, and map tiles have also begun to obtain the default root through `runtime_paths`. The next step is to make the remaining file writing point and env parsing receive the same set of helpers, and to incorporate path safety smoke into stability testing.

Goal: Avoid duplicate implementation of `TRAIL_MATE_SD_ROOT`, `TRAIL_MATE_SETTINGS_ROOT`, `HOME`, and `APPDATA` fallbacks for each runtime file.

Duplicate points that still need to be closed:

- `map_tiles.cpp` has been changed to `runtime_paths`, but the old `kSdRootEnv` / `kSettingsRootEnv` constants still remain, which can easily cause misreading or compilation alarms
- `pack_repository_runtime.cpp` still pushes repo root from `__FILE__` by default, without entering the runtime root model
- `settings_store.cpp` has been used with `settings_file()`, but temp file writing has not been reused `safe_write_under_root()`
- `route_storage.cpp` and `tracker_runtime.cpp` has been partially converted to use path helper, but it still needs to unify writing and list semantics
- There are still some old env constants or comments in `device_runtime.cpp`, `hostlink_runtime.cpp`, `team_ui_store_runtime.cpp`, `sstv_runtime.cpp` that need to be cleaned up to avoid latecomers from misjudging the real configuration source
- `runtime_paths.h` still says `safe_write_under_root()` will fsync, but `.cpp` has made it clear that it is currently just close + atomic rename

It already has a basic form, and will continue to expand in this direction:

```cpp
namespace platform::linux_runtime
{
struct RuntimePaths
{
    std::filesystem::path settings_root;
    std::filesystem::path sd_root;
    std::filesystem::path cache_root;
    std::filesystem::path state_root;
};

RuntimePaths resolve_paths();
std::filesystem::path settings_file(const char* ns);
std::filesystem::path sd_child(std::string_view relative);
bool resolve_child_under_root(const std::filesystem::path& root,
                              std::string_view relative,
                              std::filesystem::path& out);
}
```

Path strategy:

- `TRAIL_MATE_SETTINGS_ROOT` explicitly specifies the settings/state root directory.
- `TRAIL_MATE_SD_ROOT` explicitly specifies the simulated SD card root directory.
- Linux host can use `$HOME/.trailmate_cardputer_zero` by default, and can be evolved to XDG later.
- Windows simulator can continue to use `%APPDATA%/TrailMateCardputerZero` by default.
- The real machine system service mode should support `/var/lib/trail-mate` or user configuration directory in the future, but do not hardcode it now.

Security rules:

- When deleting route/track, the UI only accepts file names or relative IDs, and does not accept any absolute paths.
- The platform layer must perform root containment check.
- Reject `..`, absolute path, empty name, path separator mixed ID.
- Resolve canonical/weakly_canonical before deletion, confirm that the result is still under root.

Acceptance:

- All Linux runtimes use the same set of path helpers.
- route/tracker/team/sstv/hostlink/map/pack repository no longer copies storage root logic separately.
- There are unit tests for path security.

### P5: Building Capability Authenticity Model

Return status: The concept has entered the public contract seed, and LoRa/Walkie/SSTV has exposed the public API. `CapabilityState`/`CapabilityStatus` has been moved up to `modules/core_sys/include/platform/ui/capability_status.h`, the Linux side helper only does re-export, `lora_runtime.h`, `walkie_runtime.h`, `sstv_runtime.h` has declared `CapabilityStatus capability_status()`, and the implementation layer has begun to return `Simulated`. The next step is to get contract inventory and UI rendering up to speed.

Goal: UI and user documentation can distinguish between "not supported", "simulated support", "real support" and "downgraded support".

It is recommended to add a unified state:

```cpp
enum class CapabilityState
{
    Unsupported,
    Simulated,
    Available,
    Degraded,
    Error,
};

struct CapabilityStatus
{
    CapabilityState state;
    const char* message;
};
```

 There is no need to change all contracts at once in the short term, but it cannot stay in the state of "API exists but no one consumes" for a long time. It is recommended to do three small things first:

- List `capability_status.h` in the contract inventory of `modules/core_sys/include/platform/ui/README.md`.
- Keep the declarations of `lora_runtime.h`, `walkie_runtime.h`, and `sstv_runtime.h` consistent with the Linux implementation, and use the same pattern when adding runtimes later.
- Show `Simulated` in at least one shared UI status/footer or diagnostics page to verify that the UI no longer treats synthetic runtime as real support.

Ultimately it's better to leave critical runtimes exposed:

- Wi-Fi
- GPS
- LoRa
- Walkie
- SSTV
- Hostlink
- USB mass storage
- Firmware update
- Battery/power
- Display/input

Current suggested categories:

| Features | Current Code Status | Recommended Capability Status |
| --- | --- | --- |
| Settings store | file-backed | Available |
| Time offset | settings + system clock | Available |
| Screen timeout | soft idle state | Degraded |
| Device memory | `/proc/meminfo` on Linux | Available/Degraded |
| Battery | env only | Simulated |
| GPS default data | env/default | Simulated |
| GPS NMEA file/serial | parser + Linux serial | Degraded to Available, depending on device |
| Hostlink TCP | local TCP server, default loopback | Available, external monitoring must be configured explicitly |
| Route/tracker storage | file-backed | Available with safety fix |
| Team store | memory + GPX append | Degraded |
| LoRa RSSI | synthetic | Simulated |
| Walkie | synthetic | Simulated |
| SSTV | generated frame | Simulated |
| Wi-Fi | unsupported | Unsupported |
| USB mass storage | unsupported | Unsupported |
| Firmware update | unsupported | Unsupported |
| Orientation | empty | Unsupported |

Acceptance:

- UI will not display synthetic LoRa/walkie/SSTV as real hardware capabilities.
- Documentation and status message can explain why it is not available or only simulated.

### P6: Unpack the real Linux app facade and demo world

Return status: The second step is completed. `linux_demo_world.*` has been added, and demo seeding has been gated by `runtime_mode`; but the facade still directly owns loopback mesh, dummy crypto, and loopback pairing. The next step is to let the runtime mode determine the complete composition, rather than letting the real device inherit the fake implementation of communication and encryption from the simulator demo world by default.

Goal: Separate the "fake world for simulator use" and the "real device facade".

Currently `MinimalLinuxAppFacade` also does:

- app config persistence
- service composition
- demo peers seed
- loopback mesh adapter
- team pairing loopback
- dummy team crypto
- event bus bridge
- fallback team UI updates

Recommend splitting:

```text
platform/linux/common/src/app/
  linux_app_facade.cpp
  linux_app_facade.h
  linux_demo_world.cpp
  linux_demo_world.h
  linux_loopback_mesh_adapter.cpp
  linux_loopback_mesh_adapter.h
  linux_team_runtime.cpp
  linux_team_runtime.h
  linux_app_composition.cpp
  linux_app_composition.h
```

Then distinguish composition mode:

```cpp
enum class LinuxRuntimeMode
{
    SimulatorDemo,
    DeviceLocal,
    DeviceRealMesh,
};
```

Key requirements:

- dummy crypto can only be used in `SimulatorDemo`.
- real device path disables XOR crypto pseudo implementation.
- demo peer seed can only be enabled by simulator or explicit demo mode.
- The app facade should not know "whether the display is SDL or fbdev".

Acceptance:

- There is still demo data after starting the simulator.
- Starting device shell does not inject fake peers by default unless `TRAIL_MATE_DEMO_WORLD=1` is explicitly set.
- dummy crypto will not enter the real device release build.

### P7: Let `M5Stack_Linux_Libs` become the real machine main path

Target: The real Cardputer Zero device path gives priority to using the LVGL/fbdev/evdev/device plumbing of the SDK.

Steps:

1. Keep the display/input detect logic in `apps/linux_rpi/main/src/main.cpp`, but replace bring-up UI with shared shell session.
2. In SDK main:

```cpp
lv_init();
initLinuxDisplay();

trailmate::cardputer_zero::linux_ui::ShellSession shell;
shell.begin();

while (true)
{
    shell.tick();
    lv_timer_handler();
    usleep(1000);
}
```

The actual code is subject to the split `ShellSession` API.

3. `config_defaults.mk` Continue to enable:

```text
CONFIG_V9_5_LV_USE_LINUX_FBDEV=y
CONFIG_V9_5_LV_USE_EVDEV=y
```

4. Automatically detect framebuffer:

- First `LV_LINUX_FBDEV_DEVICE`
- Secondly `/proc/fb` `fb_st7789v`
- Finally `/dev/fb0`

5. Automatically detect keyboard:

- Prioritize `LV_LINUX_KEYBOARD_DEVICE`
- Secondly, by-path/by-id candidates
- Finally, clearly report an error but do not interrupt the display

Acceptance:

- `bash apps/linux_rpi/scripts/build-sdk-device.sh` can build shared shell.
- Real machine can display boot/menu shell.
- Real machine keyboard can navigate.
- No longer use SDK bring-up placeholder copy as the default UI.

### P8: Choose the first real feature verification slice

It is recommended to choose `Settings` for the first real page, reason:

- It relies on these basic contracts of settings/time/screen/device.
- It does not require real radio, GPS, audio, network.
- It can verify shared UI, platform contracts, persistence, input, and capability status.
- It exposes the true quality of the Linux common runtime.

Settings slice should verify:

- timezone offset saved/read.
- screen timeout save/read.
- notification volume save/read.
- battery/GPS/Wi-Fi/USB/FOTA capability status is displayed correctly.
- Settings persist after restarting simulator/device.
- No unexecutable actions will appear in the UI when there is no device capability.

Acceptance:

- The simulator performs real settings through the Settings page.
- rpi SDK path is actually set through the Settings page.
- smoke test or integration test to verify settings persistence.

## 5. Principles of elegant adaptation to Linux

### 5.1 Platform contracts are implemented before the platform

If a function needs to be used by `ui_shared`, first confirm whether it belongs to the existing `platform::ui::*` contract.

- The existing contract is enough: only add Linux implementation.
- Missing fields in contract: extend the contract first, then look at the ESP/Linux implementation at the same time.
- No contract: first determine whether this is a shared capability, a platform capability, or an app shell private capability.

Do not implement pushback shared APIs from Linux. Convenient implementation does not mean correct abstraction.

### 5.2 Keep app shell thin

`apps/linux_sim` and `apps/linux_rpi` should mainly do:

- main/entrypoint
- build configuration
- target-specific composition
- process lifecycle
- CLI/env parsing
- selecting adapters

Don't put them in the app shell for a long time:

- business state
- page layout
- protocol handling
- storage format
- radio/GPS/audio logic

### 5.3 common only puts truly shared Linux-safe code

Standards for entering `platform/linux/common`:

- Both simulator and rpi are required.
- Does not rely on SDL.
- Does not depend on fbdev/evdev/ioctl.
- Does not depend on specific device path.
- Does not contain device-specific assumptions.
- Does not own the UI page structure.

If it is only needed for desktop simulator, put `apps/linux_sim`.

If only Pi OS/Cardputer Zero needs it, put `platform/linux/rpi` or `apps/linux_rpi`.

### 5.4 Real, simulated, unsupported Must be honest

Linux early development requires a lot of simulation capabilities, which is reasonable. But it must be marked honestly.

Recommended rules:

- synthetic runtime can be used with simulator.
- synthetic runtime can be used for real machine debugging, but must be explicitly opt-in.
- The default UI of real devices should not display synthetic runtime as a hardware capability.
- `is_supported()` should not mean "there is a code path", but "can be reasonably used by users under this target".

### 5.5 No need to use `#ifdef` to replace the boundary

Allowed `#if defined(...)`:

 - Isolate OS APIs such as hostlink socket, GPS serial, gmtime within platform implementation `.cpp`.
- Select target in build system.
- Handle Windows/Linux differences in a small amount of cross-host simulator code.

Deprecated `#if defined(...)`:

- Determine Linux/ESP in `modules/core_*`.
- Determine Linux/ESP in `modules/ui_shared` page.
- Determine the device model in business logic.

### 5.6 SDK is a device dependency, not an architecture source

`M5Stack_Linux_Libs` should help us write less fbdev, evdev, and LVGL glue.

It should not determine:

- Trail Mate's module boundaries.
- app/page structure.
- Interface for core/usecase.
- storage/schema.
- Linux common responsibilities.

### 5.7 Write tests as long as the boundaries can be defended by tests

Documentation is necessary, but not enough. Every point that is prone to degradation should have automatic checks as much as possible:

-include pollution: boundary check already exists.
- path security: Added unit test.
- capability truth: Added status test.
- CMake source ownership: Reduce manual errors through shared cmake helper.
- simulator/device build: CI continues to run.
- SDK path: Provide at least optional CI or manual verification script.

## 6. New function development process

Any new Linux adaptation function follows this process.

### Step 1: Classify first

Answer first:

- Is this domain/usecase/protocol? Yes, it belongs to `modules/core_*`.
- Is this a LVGL shared page/component? Yes, it belongs to `modules/ui_shared`.
- Is this a platform contract? Yes, it belongs to `modules/core_sys/include/platform/ui/*`.
- Is this a Linux implementation? If so, it belongs to `platform/linux/common` or `platform/linux/rpi`.
- Is this process startup/target selection/build wiring? If so, it belongs to `apps/linux_*`.

### Step 2: Confirm the contract

Check `modules/core_sys/include/platform/ui/*`.

If there is an existing contract:

- No new parallel util will be added.
- Do not directly access Linux files, sockets, and env in the UI.

If there is no contract:

- Design the minimum contract first.
- The contract only expresses shared semantics, not Linux details.

Example:

```cpp
namespace platform::ui::wifi
{
bool is_supported();
bool load_config(Config& out);
bool save_config(const Config& config);
Status status();
}
```

Should not appear in the contract:

```cpp
std::filesystem::path;
int fd;
struct input_event;
sockaddr_in;
lv_indev_t*;
```

### Step 3: Implement Linux common or rpi adapter

Placement rules:

| situation | location |
| --- | --- |
| file-backed settings | `platform/linux/common/src/platform/ui/settings_store.cpp` |
| generic host TCP transport | `platform/linux/common/src/platform/ui/hostlink_runtime.cpp` |
| generic NMEA parser | `platform/linux/common/src/platform/ui/gps_runtime.cpp` or tear out helper |
| `/dev/input/event*` | `platform/linux/rpi/src/platform/device` |
| `/dev/fb*` mmap/ioctl | `platform/linux/rpi/src/platform/device` |
| SDL geometry/input | `apps/linux_sim/src/platform/simulator` |
| M5Stack SDK glue | `apps/linux_rpi/main` or `platform/linux/rpi` |

### Step 4: UI consumes through contract

`modules/ui_shared` can only:

- call the `platform::ui::<feature>` contract.
- Render enabled/disabled/unsupported state based on status/capability.
- Trigger contract action.

Cannot:

 - Read Linux env.
- Open file.
- Access `/dev/*`.
- Introduce `platform/linux/*` headers.

### Step 5: Add test

Minimum test level:

- contract smoke: whether it can be called and whether it returns to a reasonable default state.
- persistence test: Set whether it can be saved/read.
- path safety test: Whether illegal paths are rejected.
- simulated data test: GPS NMEA, hostlink, tracker, team, etc.
- UI smoke: If the function affects the shell, ensure that the simulator boot/menu/page does not crash.

Linux tests can currently be placed in:

```text
apps/linux_sim/tests/
```

With enhanced modularity, they can be gradually migrated to:

```text
tests/linux/
tests/modules/
```

### Step 6: Update build

Short term:

- Change shared CMake source helper.
- Make sure that both `apps/linux_sim` and `apps/linux_rpi` use the same source list.

Mid-term:

- Turn `modules/core_*` into CMake targets.
- `ui_shared` also becomes a target.
- The app shell only links targets and does not write internal files.

### Step 7: Update documentation

After each Linux adaptation slice is completed, update at least:

- The current status of this article or backlog.
-corresponds to feature document.
- How it works in the app README.
- Update the capability table if the capability status changes.

## 7. Guide to key implementation details

### 7.1 CMake

The current problem is that both Linux apps have large source lists written by hand. The elegant approach is to centralize source ownership.

Short-term recommendation:

```text
cmake/TrailMateLinuxSources.cmake
```

And in two apps:

```cmake
include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

trailmate_add_linux_common(trailmate_cardputer_zero_linux_common)
trailmate_add_linux_ui_shell(trailmate_cardputer_zero_ui_shell trailmate_cardputer_zero_linux_common)
```

Note:

- SDL3 of `FetchContent` should only be present in the simulator target.
- The LVGL of `FetchContent` can be provided by the shared helper, but the SDK path does not necessarily follow this LVGL.
- `apps/linux_rpi` CMake path can continue to use fetched LVGL, and SDK SCons path uses SDK LVGL.
- Don't let root `CMakeLists.txt` be hijacked by Linux targets; root still has historical ESP-IDF responsibilities.

### 7.2 LVGL

LVGL adaptation is most likely to get out of control, and the owner must be clear.

Rules:

- `lv_init()` can only be called by one host layer in a process.
- A display's buffer and flush callback are only owned by the creator.
- `ui_shared` creates an object graph without owning a Linux display.
- `ShellSession` only controls the Trail Mate session life cycle, regardless of the OS display.
- The simulator can copy the LVGL RGB565 buffer to the SDL shell through the Canvas host.
- Real device SDK path applies native LVGL display without Canvas copy.

Need to avoid:

- Include Linux headers in `ui_shared`.
- Write Linux special cases in every page.
- Adding another layer of Canvas to the SDK path will lead to poor performance and responsibilities.

### 7.3 Input

The current shared shell input semantics is a mapping of `InputEvent` to LVGL keys. Just keep this model in the future.

Implementation suggestions:

- simulator: Continue SDL event -> `InputEvent`.
- custom framebuffer path:evdev -> `InputEvent` -> `ShellSession`.
- SDK native path: Prioritize LVGL evdev; if unified behavior is required, introduce evdev -> `InputEvent`.

Input adapter to solve:

- key down/up/repeat.
- modifier.
- printable ASCII.
- back/home/next/power semantics.
- graceful degradation without input device.

### 7.4 Storage

All Linux storage should use a unified root resolver.

Recommended convention:

| Purpose | env override | Default |
| --- | --- | --- |
| settings/state | `TRAIL_MATE_SETTINGS_ROOT` | `$HOME/.trailmate_cardputer_zero` or Windows `%APPDATA%` |
| SD-like files | `TRAIL_MATE_SD_ROOT` | `sdcard` under settings root |
| packs | `TRAIL_MATE_PACK_ROOT` | `packs` under repo root |
| GPS NMEA file | `TRAIL_MATE_GPS_NMEA_FILE` | None |
| GPS serial | `TRAIL_MATE_GPS_DEVICE` | No automatic opening |

File writing:

- Use temp file + fsync + rename for small files.
- settings namespace filename must be sanitized.
- blob It is recommended to continue hex encoding or migrate to an explicit binary file, which cannot mix in unescaped delimiters.
- Delete actions must be root-contained.

### 7.5 GPS

Currently `gps_runtime.cpp` already has a good foundation:

- env ​​default location.
- NMEA RMC/GGA/GSA/GSV parsing.
- Linux serial nonblocking read.
- file source incremental read.
- stale detection.
- GNSS satellite snapshot.

Subsequent improvements:

- Split the NMEA parser into a single testable helper.
- Add checksum strict mode configuration.
- Add serial reconnect backoff.
- Add device discovery, but do not open all `/dev/tty*` by default.
- The real device UI should show source: simulated/env/file/serial/stale.

### 7.6 Hostlink

The current hostlink TCP runtime is available, but the default security posture needs to be adjusted.

Suggestion:

- The default bind of simulator is `127.0.0.1`.
- When a real machine wants to monitor externally, it must explicitly set `TRAIL_MATE_HOSTLINK_BIND=0.1.30-alpha.0`.
- Endpoint file written to `hostlink/endpoint.txt` under SD root can be retained.
- If sensitive operations are carried in the future, authentication or pairing is required. Do not rely solely on LAN isolation.

### 7.7 LoRa、Walkie、SSTV

The current three blocks are mainly synthetic runtime:

- LoRa: Generate RSSI curve.
- Walkie: Generate tx/rx level.
- SSTV: Generate frame and save PPM.

Short term retention value:

 - Help UI page and flow are verifiable on Linux.
- Support simulator demo.

Boundaries that must be filled:

- status displays simulated.
- Real devices should not claim that radio/audio is available by default.
- Before real radio/audio is connected, do not let the upper layer protocol rely on these synthetic behaviors.

### 7.8 Team and Chat

The current Linux facade has made chat/contact/team very vital in the simulator, but this part is most needed to prevent concept drift.

To be broken down clearly:

- The domain/usecase/infra store of `core_chat` is a reusable core.
- Linux loopback mesh is a simulator adapter.
- demo contacts is the simulator data seed.
- team dummy crypto is demo-only.
- The memory snapshot and GPX append in the team UI store are early runtimes and are not equivalent to the complete implementation of real team persistence.

Must do before real device:

- Replace dummy crypto.
- Turn demo seed into opt-in.
- Clarify what mesh transport: LoRa, BLE, hostlink, and local loopback are.
- Let the persistence format of the contact/node store have versions and migration strategies.

### 7.9 Wi-Fi、USB、FOTA、Orientation

Most of these are currently unsupported or empty implementations. The correct strategy is to continue explicitly unsupported until there is a real Linux path.

Don't return `true` for menu integrity.

The UI should show unavailable based on the status, rather than going to a semi-functional page.

### 7.10 Threads and Life Cycle

 Hostlink worker thread is already available in Linux common. Follow the following rules when adding new threads:

- `start()` is idempotent.
- `stop()` must be joined.
- Destruction does not throw an exception.
- Background thread cannot call LVGL.
- shared state is explicitly protected with mutex/atomic.
- Test to cover start-stop-start.

### 7.11 Error handling

Principle:

- app shell main can catch exception and return non-zero.
- The platform runtime contract should try to return `Status` or bool + message.
- Real machine device path failure must be diagnosable: output device path, errno, fallback.
- If the simulator script fails, please give installation suggestions.

### 7.12 Environment variable naming

Continue to use the `TRAIL_MATE_` prefix.

Suggested rules:

- runtime root:`TRAIL_MATE_SETTINGS_ROOT`、`TRAIL_MATE_SD_ROOT`
- simulator:`TRAIL_MATE_SIM_*`
- GPS:`TRAIL_MATE_GPS_*`
- hostlink:`TRAIL_MATE_HOSTLINK_*`
- demo:`TRAIL_MATE_DEMO_*`
- device:`TRAIL_MATE_FBDEV`、`TRAIL_MATE_INPUT_DEVICE`

Environment variables are startup configuration and test injection and should not be the only source of business persistence.

## 8. Specific backlog

### 8.1 Do it immediately

1. Repair the current compile gate.

Acceptance: simulator common, UI shell, path safety smoke, rpi framebuffer target can all be compiled. 2026-05-08 This round has repaired the LVGL callback signature and the `kKeyMap` initialization risk of `evdev_input.cpp`. The next step should be to directly run the complete CMake build under Linux/WSL.

2. Stable Linux CMake source helper.

Acceptance: sim/rpi CMake no longer repeats the large source list; rpi-only source has a clear entry; only one change is made when adding shared UI source files.

3. Return to `ShellSession` and LVGL input details.

Acceptance: simulator behavior remains unchanged; a single key enqueue can generate press/release; `continue_reading` does not swallow events; SDK path can reuse sessions without being blocked by `lv_init()` ownership conflicts.

4. Complete the `platform/linux/rpi` evdev input adapter.

Acceptance: custom framebuffer shell can be navigated by keyboard; `TRAIL_MATE_INPUT_DEVICE=/dev/input/eventX` can be overwritten; only downgrade and print diagnostics when there is no input device; Cardputer Zero's built-in keyboard Fn/key combination mapping comes from real machine event code sampling.

5. Expand runtime path helper coverage.

Acceptance: settings, route, tracker, sstv, team, hostlink, map tiles, pack repository common path resolution; absolute path, `..`, cross-root deletion are rejected; old env constants of map/hostlink/team/SSTV and inaccurate fsync comments in `runtime_paths.h` are cleared.

6. Fix and incorporate path traversal tests.

Acceptance: absolute path, `..`, and cross-root deletion are all rejected.

7. Establish capability truth table and update UI status.

Acceptance: `capability_status.h` appears in contract inventory; public headers such as LoRa/Walkie/SSTV continue to expose `capability_status()`; at least one UI status entry consumes the API; synthetic functions are no longer shown as real support.

8. Let runtime mode truly control facade composition.

Acceptance: `TRAIL_MATE_RUNTIME_MODE=demo/local/mesh` can change the demo seed, loopback mesh, dummy crypto, and real device capability presentation; the demo world is not enabled by default on the real machine, and dummy crypto and loopback pairing are not combined by default.

9. Verify simulator runtime mode default value writing semantics.

Acceptance: Both POSIX and Windows only write `demo` when `TRAIL_MATE_RUNTIME_MODE` is not set; it will not be overwritten by simulator main when the user explicitly sets `local` or `mesh`.

### 8.2 The first round of real device closed loop

1. Change `apps/linux_rpi/main/src/main.cpp` from bring-up UI to shared shell.
2. Keep `LV_LINUX_FBDEV_DEVICE` and `/proc/fb` auto-detect.
3. Keep `LV_LINUX_KEYBOARD_DEVICE` to add more robust input detection.
4. Record startup log: display path, input path, settings root, sd root, capability mode.
5. Verify boot/menu/settings on the real machine.

Acceptance:

- SDK path built successfully.
- Real machine displays shared shell.
- The real machine can be operated by buttons.
- Settings persistence.

### 8.3 First round of function migration

Priority:

1. Settings
2. Contacts/Chat local store
3. GPS page with NMEA source status
4. Tracker file workflow
5. Hostlink page
6. Team local runtime cleanup
7. Map tile/file storage

On hold:

- real LoRa
- real walkie/audio
- Wi-Fi provisioning
- USB mass storage
- firmware update
- BLE phone integration

### 8.4 CI and verification enhancements

New suggestions:

- Linux CMake source helper smoke.
- path safety test.
- NMEA parser unit tests.
- hostlink start/stop/restart test.
- capability status test.
- optional Docker image build check.
- optional SDK build job, which needs to be able to cache or fix the SDK acquisition method.

Currently existing commands:

```bash
python3 scripts/check_platform_ui_boundaries.py

cd builds/linux_cmake
cmake --preset linux-simulator-debug
cmake --build --preset linux-simulator-debug-build
ctest --preset linux-simulator-debug-test

cmake --preset linux-uconsole-debug
cmake --build --preset linux-uconsole-debug-build
ctest --preset linux-uconsole-debug-test

cmake --preset linux-uconsole-release
cmake --build --preset linux-uconsole-deb
```

Windows/WSL simulator:

```powershell
wsl.exe --exec bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate/builds/linux_cmake && cmake --preset linux-simulator-debug && cmake --build --preset linux-simulator-debug-build && ctest --preset linux-simulator-debug-test'
```

Windows/WSL uConsole:

```powershell
wsl.exe --exec bash -lc 'cd /mnt/c/Users/VicLi/Documents/Projects/trail-mate/builds/linux_cmake && cmake --preset linux-uconsole-debug && cmake --build --preset linux-uconsole-debug-build && ctest --preset linux-uconsole-debug-test'
```

SDK device:

The current repository does not have an active Cardputer Zero Linux CI entry. This target needs to be completed with independent device shell, SDK build entry and hardware verification gate, and then be listed here as a special command.

## 9. Code review checklist

Each Linux adaptation PR should check:

- Whether the `modules/core_* -> platform/*` dependency has been added.
- Whether the `modules/ui_shared -> platform/linux/*` dependency has been added.
- Whether to put simulator-only code into `platform/linux/common`.
- Whether to put Pi-specific code into `platform/linux/common`.
- Whether to add unexplained `#ifdef __linux__`.
- Whether there are new file operations that can be affected by path traversal.
- Whether to mark synthetic runtime as supported.
- Whether to let the app shell grow business logic.
- Whether the CMake shared source helper has been updated.
- Is there at least smoke or unit test.
- Whether capability status and documentation are updated.

## 10. Definition of Done

Linux adaptation cannot only use "can compile" as the completion standard.

A Linux slice is completed, at least meeting the following requirements:

- The contract location is correct.
- The implementation is in the correct position.
- The simulator can run.
-rpi CMake target is not degraded.
- If a real machine is involved, the SDK path has a verification path.
- Honest ability status.
- synthetic/demo behavior does not pollute real devices by default.
- File paths are safe.
- The life cycle can be stopped and restarted without obvious leakage.
 - CI or manual verification command explicit.
- Document synchronization.

When the entire Cardputer Zero Linux adaptation can truly be called complete, it should meet the following requirements:

- `apps/linux_sim` is a stable development and verification tool.
- `apps/linux_rpi` SDK path is the real device main path.
- `platform/linux/common` does not contain simulator/device special case.
- `platform/linux/rpi` only contains device-specific adaptations.
- The shared shell is the same on the simulator and the real machine.
- Settings, Chat/Contacts, GPS, Tracker, Hostlink at least have a real Linux runtime.
- Capabilities such as LoRa/Walkie/SSTV/Wi-Fi/USB/FOTA are either truly available or honestly unsupported/simulated.
- The boundary between `modules/core_*` and `modules/ui_shared` is guarded by automatic checking.
- Adding a new Linux device family does not require copying the existing app shell.

## 11. Recommended roadmap

### Milestone A: Structural closure

Goal: Reduce duplicate and expired documents.

- Pump CMake source helper.
- Update app-local specs.
- Create path/capability helper.
- Add path safety tests.

### Milestone B: Close the real machine shell

Goal: Let the SDK path run the Trail Mate shared shell.

- Detach ShellSession.
- SDK main connects to shared shell.
- evdev input is available.
- Clear startup log.

### Milestone C: Settings verification slice

Goal: Verify the minimum true functional closed loop.

- Settings page is available in simulator/device.
- Settings persistence is really on the market.
- capability status is trusted in the UI.

### Milestone D: Data and Communication Basics

Goal: Make Linux devices more than just UI demos.

- Chat/contact/node store organized.
- Hostlink security default.
- GPS NMEA serial real machine verification.
- Tracker/route file workflow.

### Milestone E: Hardware capabilities

Goal: Gradually connect to real device capabilities.

- Battery/power.
- Brightness/backlight.
- Audio/walkie.
- Radio/LoRa.
- Wi-Fi.
- USB/FOTA, if required by device and product form.

### Milestone F: Release and Maintenance

Goal: Enter the available product lines from the development shell.

- systemd service or startup script.
- Configure directory and permission policies.
- Logging policy.
- release artifact.
- Equipment regression manual.

## 12. Current final judgment

The current Linux adaptation has completed the most difficult first thing: establishing the target structure and boundary awareness, and giving the simulator and Linux common runtime the basis for reusing the shared UI. According to the latest regression on 2026-05-08, the code has further made structural advancements in the right direction such as CMake helper, LVGL session/host layering, evdev input, runtime path/env, capability status moved up and some public APIs, demo world extraction, SDK bring-up enhancement, etc.

But this round of advancement has not yet reached "adaptation completion". The key to the next stage is not to continue to pile on more simulation functions, but to first consolidate these structural changes into buildable, testable, and runnable facts:

- Repair the compile gate first and make sim/rpi/test green again.
- Verify LVGL input callback again to make key event semantics reliable.
- Then close the real machine input/display main path, especially rpi CMake, SDK main, and evdev real machine verification.
- Then close the path/capability/runtime configuration so that the pack repository path is no longer free and the capability contract inventory and UI rendering can keep up.
- Then close the boundary between the demo and the real device facade so that the real device does not inherit the simulated world by default.

If we proceed in this order, the Linux line will naturally grow into Trail Mate's first-class platform rather than a new parallel project.
