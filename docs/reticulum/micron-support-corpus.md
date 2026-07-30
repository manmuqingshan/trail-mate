# Network Micron Support Corpus

Trail Mate's Network page intentionally implements a bounded Micron renderer. The
goal is to keep small Nomad pages readable and safely interactive on constrained
devices, not to become a full desktop NomadNet client with resource rendering or
recursive partial execution.

Current corpus sample:

| Sample | Path | Size | Purpose |
| --- | --- | ---: | --- |
| Trail Mate showcase | `docs/reticulum/pages/trail-mate/index.mu` | 3366 bytes | Real page used to describe Trail Mate and exercise the supported renderer subset |
| NomadNet message board capture | `docs/reticulum/pages/corpus/nomadnet-messageboard.mu` | bounded | Rendered capture derived from the NomadNet 1.2 message-board example |
| NomadNet forms | `docs/reticulum/pages/corpus/nomadnet-forms.mu` | bounded | Text, password, checkbox, radio, selected fields, `*`, and constant variables |
| Resilience/fallback | `docs/reticulum/pages/corpus/nomadnet-resilience.mu` | bounded | Anchors, pages, resources, partials, literal/fallback and unknown tags |

Compatibility table for the current sample:

| Micron feature | Example in corpus | Current Network behavior | Runtime cost policy |
| --- | --- | --- | --- |
| Header colors | `#!fg=ddd`, `#!bg=050505` | Applies page foreground/background defaults | Parsed once per page |
| Comments | `# Trail Mate Micron showcase page...` | Skipped | No objects |
| Headings | `>Trail Mate`, `>>Overview` | Rendered with heading styling and auto anchors | One row per heading |
| Explicit anchors | `` `:overview `` | Registered for same-page jumps | Bounded by anchor pool |
| Non-ASCII text | Chinese sentence in the engine intro | Rendered as UTF-8 text; slug helper preserves UTF-8 heading text | No transcoding buffer |
| Inline emphasis | `` `!Bold`! ``, `` `_Underline`_ `` | Bold/underline state is reflected in labels | Chunked text labels |
| Inline colors | `` `F6cf...`f ``, `` `B123...`b `` | Supports 3-digit, 6-digit true color, grayscale, and split color tokens | Shared contract parser |
| Alignment | `` `c ``, `` `l ``, `` `r ``, `` `a `` | Changes row flex alignment until reset | No persistent allocation |
| Escapes/reset | `\`not-a-tag`, double tick reset | Escaped text remains literal; reset clears style | Inline scan only |
| Dividers | `-*`, `-=` | Rendered as repeated divider text | One row |
| Literal block | `` `= `` block in engine section | Rendered as preformatted display text with subtle block styling | One row per literal line |
| Page links | `` `[`Same page at limits`/page/index.mu`anchor=limits] `` | Clickable when target is page or anchor | Bounded link pool |
| Anchor links | `` `[`Overview`#overview] `` | Clickable same-page jump | Bounded link pool |
| Link anchor hint | `anchor=matrix` | Appends `#matrix` before navigation | Contract-tested |
| Submit fields | `callsign|offline|transport|*` | Sends selected fields as `field_*`; `name=value` becomes `var_name` | Max 15 values / 256-byte MsgPack map |
| Text fields | `` `<18|callsign`TRAIL-MATE> `` | Editable single-line field | Shared max 12 form controls |
| Masked fields | `` `<!18|token`hidden> `` | Editable password-style field | Shared max 12 form controls |
| Checkbox/radio fields | `` `<?...>``, `` `<^...>`` | Interactive; radio values with the same name are mutually exclusive | Shared max 12 form controls |
| Tables | `` `tc42 ``, `` `tr38 ``, `` `tl36 `` | Bounded table rows with header styling and column alignment | Max 6 cells per row |
| Table separators | `|:--|:--:|--:|` | Updates column alignment; separator row is not rendered | Contract-tested |
| Page partials | `` `{:/page/live-status.mu`30} `` | Shows unsupported page partial and an `Open partial` page link | No inline fetch |
| Resource links | `` `[`Field guide PDF`:/file/...pdf] `` | Shows label plus `[resource unsupported]`; not clickable | No resource fetch |
| Unknown tags | `` `q `` | Counts and samples unsupported tags in a notice | Small stack-only sample set |
| Render limits | Link, anchor, row, object caps | Shows limit notices instead of growing unbounded | PSRAM-first pools where dynamic |

Out of scope for this renderer:

| Feature | Reason |
| --- | --- |
| Recursive partial execution | Would add nested network/cache failure states and unpredictable object growth |
| Image/file/resource rendering | High memory and transfer cost; current behavior is explicit fallback |
| Full upstream layout parity | Flash is already tight, and exact desktop-like layout is not the product goal |
