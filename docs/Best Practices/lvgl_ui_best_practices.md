# LVGL UI code best practices: beautiful writing, comfortable reading, painless modification

## 0. Determine the "order of reading the code" first

The biggest problem of LVGL is not the API, but **Imperative + Style Noise**:
A bunch of `set_style_*` drowns out the "structural intent".

So a reading order must be forced:

1. **layout**: object tree and layout constraints (structure)
2. **styles**: Visual rules (skin)
3. **components**: Refresh process and business (when to draw what)
4. **input**: Knob/keyboard/touch focus flow (interaction)

> Let readers see the code like a UI prototype.

---

## 1. Each page must have a "wireframe + tree structure diagram"

Institutionalize your current approach:

* `*_layout.cpp` must have an ASCII wireframe in the header
* Also provide a Tree view (object tree)

so that readers can know what the UI looks like without reading a line of LVGL API.

**Rule**: Wireframes describe "structure" and do not describe style details such as "color/rounded corners".

---

## 2. Split files into layers: layout / styles / components / input

### 2.1 layout: only write structure, no style, no business

Layout allowed APIs:

* `lv_obj_create/lv_btn_create/lv_label_create`
* `lv_obj_set_size`
* `lv_obj_set_flex_flow / lv_obj_set_flex_grow / lv_obj_set_flex_align`
* `lv_obj_align / lv_obj_center`
* `lv_obj_clear_flag(SCROLLABLE) / set_scrollbar_mode`

layout **forbidden** appears:

* `lv_obj_set_style_*` (all prohibited)
* `lv_obj_add_event_cb` (events belong to the logic layer)
* Data formatting, paging, filtering (business belongs to components)

layout should provide "structural function":

* `create_panels(...)`
* `ensure_subcontainers()`
* `create_list_item_struct(...)`

### 2.2 styles: centrally define `lv_style_t`, provide `apply_*`

styles rules:

* `init_once()` idempotent initialization, avoid repeated `lv_style_init`
* **Semantic style name**: `btn_basic`, `btn_primary`, `panel_side`, `list_item`
* Use `LV_STATE_*` for status styles (for example, use `LV_STATE_CHECKED` for selection)

styles **forbidden**:

* Business logic
* Create object

You now use `apply_btn_filter()` + `LV_STATE_CHECKED` to highlight, which is the standard writing method.

### 2.3 components: business refresh and life cycle

components are responsible for:

* `refresh_ui()`: delete old and create new/incremental update, paging, button enable/disable, selected state refresh
* Event callbacks (such as Contacts/Nearby switching)
* Data formatting (format_time_status/format_snr)
* Modal creation/cleaning

Rules for components:

* **Long paragraphs of style** are prohibited (only call `style::apply_*`)
* **It is forbidden to create new layout structures** (structures can only pass `layout::*`)

### 2.4 input: focus and navigation (rotary/keys)

Input is responsible for:

* lv_group focus management (how to jump between 3 columns)
* rotary's rotate/press mapping (scroll up and down/switch left and right/trigger when pressed)
* Not directly manipulating styles, but driving UI updates through "selected state/focus state"

Input rules:

* The input does not directly change the UI details, but changes the state: `selected_index / focused_column / current_page`
* Then call `refresh_ui()` (or finer-grained update)

---

## 3. Three iron rules to completely eliminate "style noise"

### Iron rule 1: Any `set_style_*` are all "debt"

Once you directly `lv_obj_set_style_bg_color(...)` in a certain page,
you will quickly copy it 10 times on 10 pages.

**Correct approach**: Change to `apply_*()`, and pass state if differences are needed.

### Iron rule 2: The status is expressed through LV_STATE instead of changing the color

* Selected: `LV_STATE_CHECKED`
* Disabled: `LV_STATE_DISABLED`
* Focus: `LV_STATE_FOCUSED`

The style changes with the state, and the logic layer only changes the state.

### Iron Rule 3: Don't let "visual details" appear in `refresh_ui()`

`refresh_ui()` should look like a flow chart:

* Select data
* Count page numbers
* Create 4 rows
* Update button status
* Update selected state

Any visual details should be `style::apply_*()`.

---

## 4. Unify "component naming" and "object tree convention"

A large part of the readability of LVGL code comes from naming.

Recommended convention:

* panel:`filter_panel/list_panel/action_panel`
* container:`sub_container/bottom_container`
* item:`list_items[]`
* button:`*_btn`
* label:`*_label`

At the same time, clarify in `contacts_state` which objects are "long-term existence" and which are "refresh reconstruction":

* Long-term existence: panel, sub_container, bottom_container, pagination buttons
* Reconstruction: list_items (delete and rebuild each refresh)

---

## 5. Make an "explicit statement" on the refresh strategy

LVGL often has confusion about "should it be deleted and rebuilt or updated incrementally".

It is recommended that you write clear strategies in components (comments or documents will do):

* List items on this page: **Rebuild** (simple and controllable)
* pagination button: **Create only once** (avoid repeated allocation)
* action panel: depends on the selection (can be incremented/rebuilt)

This will make subsequent optimization (performance, memory) clear.

---

## 6. Turn layout constants into "page specifications" instead of scattered magic numbers

For example, you have done well:

* `kFilterPanelWidth = 80`
* `kActionPanelWidth = 100`
* `kItemsPerPage = 4`

 Suggest further steps:

* Put layout constants in layout files
* Put business constants in components files
* Put style constants (color/rounded corners/borders) in styles files

**Don't mix them together**.

---

## 7. Optional advancement: make a "UI DSL" level helper (but don't overdo it)

When the number of pages increases, you will find a lot of duplication:

* create_button_with_label
* create_panel_column
* create_row_container

You can make a small amount of helpers, but keep the boundaries:

* helper only does structure (layout helper)
* helper does not do styling (styles are handed over to apply)

---

# Last paragraph: a implementation list (you can post it to CONTRIBUTING)

**Each new page must meet:**

* [ ] `*_layout.cpp` has wireframe + tree view in the header
* [ ] The layout file does not appear any `set_style_*`
* [ ] The styles file has `init_once()`, all styles pass `apply_*()`
* [ ] No visual details appear in the `refresh_ui()` of components
* [ ] input Independent file, the input only changes the status, not the style directly
* [ ] magic number is divided into three categories: layout / style / logic, each in its own place
