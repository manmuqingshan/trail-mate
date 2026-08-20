# P2 · [Candidate pending] System and Media Runtime Not yet completed Model-or-Projection classification

Status: **acknowledged**
Category: **Architectural Boundaries/Model Completeness**

## Conclusion

Conclusion that the current nine Registry models are not "enough". There are at least four groups of candidate boundaries with stable state languages ​​and life cycles in the source code, but they are mainly located in `platform/ui` or UI runtime, and cannot yet be asserted as independent domain models based on the number of types alone: ​​

1. Reticulum real-time calls: `State`, `RealtimePhase`, `Peer`, `Snapshot`, answer/reject/hang-up and audio queue.
2. Content package installation: `PackageRecord`, `InstalledPackageRecord`, `PackageInstallPhase`, installation/uninstallation and compatibility judgment.
3. Firmware update: `Phase`, `Status`, check, download, install and restart life cycle.
4. Wi-Fi resource arbitration: `Request`, `Lease`, `Decision`, `ExclusiveOwner`, preemption phase and traffic budget.

These candidates cannot continue to be invisible in the integrity review; nor can they directly package the runtime data structure into a domain model in order to increase the number of Explorers. The current correct status is "Candidate Pending Adjudication".

## Why can't it be directly registered as four models?

- Real-time calls also include business sessions, protocol interoperation, device media and resource preemption; the aggregation boundary has not yet been clarified.
- Package installation and firmware updates have a life cycle, but they may belong to the application service of Device/Capability, or they may form an independent update/content management model.
- Wi-Fi lease has a clear policy, but it is more like a cross-capability resource scheduling model; its owner is still the platform runtime.
- The four groups of APIs are mainly based on global runtime state and free functions, lacking ports decoupled from the UI, domain testing and clear persistence boundaries.

## Source code evidence

- `modules/core_sys/include/platform/ui/reticulum_call_runtime.h`
- `modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp`
- `modules/core_sys/include/platform/ui/pack_repository_runtime.h`
- `platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp`
- `modules/core_sys/include/platform/ui/firmware_update_runtime.h`
- `modules/core_sys/include/platform/ui/wifi_access_runtime.h`
- `platform/esp/arduino_common/src/platform_ui_wifi_access_runtime.cpp`

## Questions that require the author's decision

1. Will the business session of the real-time call span Reticulum and other future transport protocols, or will it only be projected for Phone/Reticulum integration?
2. Do Package and Firmware share a "device content and upgrade" model, or are they two independent application workflows?
3. Are the priority, exclusivity and preemption rules of Wi-Fi lease belong to product-level resource governance and should be owned by the Device/Capability model?
4. Which states require persistence, auditing, or recovery across restarts? State that only exists in one UI session should not be automatically promoted to a domain entity.

## Closing Criteria

Give the conclusion of one of the four choices of `independent model`, `element of existing model`, `application workflow`, `integration projection` item by item, and record the owner, invariant, port and test evidence. Only candidates that are adjudicated as independent models and whose implementation boundaries have been formed enter the Model Explorer.
