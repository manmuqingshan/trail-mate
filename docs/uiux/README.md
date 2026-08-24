# UI/UX Specification Index

`docs/uiux` is organized by "object type" rather than by file history stack.

The current directory constraints are as follows:

- `foundation/`
 - Design baseline for global style, visual language, and cross-page sharing.
 - The documentation here cannot smuggle in the layout details of a specific page.
- `pages/`
 - Put page-level specifications.
 - Each file only interprets one page object and does not interpret the reusable component body.
- `components/`
 - Put component-level specifications and component implementation specifications.
 - Only component boundaries, responsibilities, status and implementation constraints are defined here, and pages are not defined in reverse.

Current file ownership:

- `foundation/firmware_visual_style.md`
- `pages/node_info_page.md`
- `pages/node_info_page_layer_popup_addendum.md`
- `components/shared_map_viewport.md`
- `components/shared_map_viewport_impl.md`
- `components/shared_map_viewport_layer_popup_addendum.md`

If you add a new document later, you must first determine what type of object it belongs to before placing it in the directory:

1. Does it describe the global visual language, a certain page, or a certain component.
2. If it wants to define pages and components at the same time, it means that the document boundary has not been cut yet and should be split first.
3
