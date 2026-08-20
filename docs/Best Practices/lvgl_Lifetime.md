### 📌 Background and Objectives

You are designing UI interface code for **LVGL (C/C++, embedded, non-GC)**.
LVGL **has no automatic life cycle management**, any timer / event / callback / service pointer that continues to run after the object is destroyed will cause serious dangling access problems.

**Goal**:
All generated interface codes must have a life cycle management method that is **interpretable, evolvable, and provably safe**.

---

## 1. [Hard Constraints | Prohibited Matters]

The following behaviors are **all prohibited**. If requirements imply these practices, they must be proactively avoided and given safe alternatives:

1. ❌ **Prohibited from triggering callbacks after destruction**

 * Prohibited timer / async / event from still accessing its members after the screen has been destroyed
 * Prohibited from relying on "LVGL" Implicit assumption that it will be automatically cleaned up

2. ❌ **It is forbidden to directly trigger callbacks that may destroy itself in timer / event callbacks**

 * For example: calling external `done_cb` in timer callbacks, and this callback may cut the screen / delete the current screen
 * Internal cleanup must be completed first and callbacks are regarded as "life cycle boundaries"

3. ❌ **Naked use of `lv_timer_create`** is prohibited

 * All timers must be managed (can be managed centrally and deleted uniformly)
 * "Forgetting to delete timers" is not allowed Implementation of "can also run"

4. ❌ **It is forbidden to rely on the UI structure to determine business semantics**

 * It is not allowed to use methods such as `target == send_btn` to distinguish actions
* UI structural changes should not affect business logic

5. ❌ **Prohibition of unclear lifecycle ownership**

* It is forbidden to hold bare pointers (such as services) that "may die before screen"
* It must be clear: who owns whom and who must live longer

6. ❌ **Disable implicit state stacking**

* It is not allowed to use multiple Boolean variables to piece together a state machine (such as `send_ok/send_finished/send_timeout`)
 * State transfer must be explicit and enumerable

7. ❌ It is forbidden to release/destroy C++'s own memory (delete this / delete impl, etc.) within the `LV_EVENT_DELETE` callback **

 * Delete Hook is only allowed to do: mark dead, disconnect dependencies, stop asynchronous resources (timer/anim/async), clear callback pointers
 * Releasing C++ memory must be completed in the **explicit destruction path** (such as destructor/destroy()), or delayed outside the LVGL deletion stack through `lv_async_call`

8. ❌ "Delete a function timer" is prohibited Clear all timers (full clear)**

 * Timer hosting must support **Delete by handle** or **Delete by group/domain** (such as SendTimers / UiTimers / TelemetryTimers)
 * It is not allowed to accidentally delete other irrelevant timers in the page in order to stop a process (such as send)

---

## 2. [Mandatory requirement | Must have]

The generated interface code **must explicitly reflect the following mode**:

### 1 Clear "life and death mark" (Hard Guard)

* screen / controller There must be **the only identifiable "dead" state**
* During destruction, all callback reachable paths must be cut off immediately**
* All LVGL callback entries must check this status first

> Example: `if (!screen || !screen->impl_) return;`

---

### 2 Unified clearing point (Delete Hook)

* `LV_EVENT_DELETE` must be registered on the root container
* Completed in this callback:

 * timer delete
 * Clear the external callback pointer
 * Disconnect external dependencies such as service / IME / group
* The destructor must not disperse the cleanup logic

---

### 3 Timer must be centrally managed

* There must be a clear timer management strategy (such as TimerBag / list)
* At the end of the life cycle, all timers must be deleted at once
* Disable timer from holding unsafe user_data

---

### 4 callback = life cycle boundary

* External callbacks (such as `done_cb / action_cb`) must be treated as:

 > "After the call, this object may die immediately"
* Before calling the external callback:

 * All internal cleanup must be completed
* After the call:

 * No members shall be accessed again

 (use `lv_async_call` if necessary)

---

### 5 Behavioral semantics must be modeled explicitly

* All user actions must be expressed through **enum/intent types**
* UI Only responsible for triggering intentions, not explaining intentions

---

### 6 Asynchronous processes must be explicit state machines

* Use `enum class` to express stages (such as Idle / Waiting / Done)
* Explicitly allowed state transfers
* timer / event only drives state transfers and does not mix business judgments

---

### 7 Delete Hook The boundaries of responsibilities must be clear (only clean up, not release itself) **

* Must be explicitly stated: Delete Hook is only responsible for termination and cleanup, not responsible for releasing C++ structure memory
* If you need to trigger the release in the hook, you must move out of the LVGL deletion call stack through `lv_async_call` or the delay mechanism

---

### 8 Timer management must have granularity (Timer Domains)**

* There must be a clear timer domain or label (at least distinguish: SendFlow vs ScreenGeneral)
* When stopping the timer of a domain, it must not affect other domains
* Any `clear_all_timers()` can only be used when "the page is completely destroyed"

---

### 9 A "life cycle responsibility table" must be output **
* List the Owner, creation point, destruction point, and "whether processing in delete hook is allowed" for each type of resource (root obj / timers / async payload / external callbacks / services).

---

## 3. [Design Orientation | What should be done]

Under the premise of meeting the above constraints:

* **Prefer using composition rather than inheritance**
* It is not recommended to introduce complex parent classes/virtual functions unless it can significantly reduce duplication without reducing readability
* Life cycle related logic should be concentrated on:

 * Lifetime / Guard / Manager classes
 * rather than scattered in business functions

The goal is to make:

> "Write according to specifications = the most trouble-free"
> "Write not according to specifications = difficult to write / problems will be exposed as soon as you write"

---

## 4. [Output requirements]

When generating or modifying code, **must**:

1. Actively point out: how the design meets life cycle safety
2. Clearly state:

 * Where are the life cycle boundaries
 * Where callback reentrancy / dangling access is prevented
3. If there is a trade-off, the reasons must be explained

---

## 3. Optional: Strict mode (for Debug/audit)

If you want to be more ruthless, you can add a sentence at the end of Prompt:

> **Debug mode is on**:
> All potential paths that violate life cycle constraints must be explicitly pointed out and correction plans given;
> If security cannot be guaranteed under the current structure, code generation must be refused and the reasons must be stated.
