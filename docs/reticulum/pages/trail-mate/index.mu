#!fg=ddd
#!bg=050505
# Trail Mate Micron showcase page. Keep this under the device body budget.

>Trail Mate
`:top
`c`F6cf`!Trail Mate`!`f
`c`B123field companion`b for maps, mesh chat, Reticulum, GPS, teams, and SSTV.
`c`F8dfBuilt for small screens, low memory, and radio paths that disappear.
`cEnglish: This page showcases Trail Mate together with the current Micron engine capabilities.

`c`[`Overview`#overview] `[`Engine`#engine] `[`Tables`#tables] `[`Controls`#controls] `[`Limits`#limits]
-*

>>Overview
`:overview
Trail Mate is a device-first field tool. It keeps radio work practical: cache first, bounded renderers, visible degradation, and UI that stays useful when links are slow.

`l`F8fd`!What it carries`!`f
Mesh chat, maps, contacts, team status, GPS fixes, Reticulum/LXMF pages, and quick field notes.

`r`B223`FfeaDesign promise:`f`b no desktop-only assumptions hiding behind a tiny display.
`a

>>Micron engine tour
`:engine
This page is one Micron document. It uses headings, explicit anchors, inline color, emphasis, alignment, dividers, literal blocks, tables, local links, display-only fields, and visible fallbacks.

`!Bold`! highlights priorities. `_Underline`_ marks promises. `*Italic`* marks terms. `F6cfColor spans`` return to the page default with a double tick. Escaped tick: \`not-a-tag.

>>>Literal sample
`=
`[`Label`/page/index.mu`anchor=engine]
`{:/page/live-status.mu`30}
\`=
`=

`c`[`Project matrix`#matrix] `[`Same page at limits`/page/index.mu`anchor=limits]

>>Project matrix
`:matrix
`tc42
| Area | Trail Mate choice | Device reason |
|:--|:--|--:|
| UI | LVGL shared screens | readable handhelds |
| Radio | Reticulum / LXMF | async field links |
| Mesh | Meshtastic / MeshCore | team channels |
| Storage | SD cache first | offline recovery |
| Pages | Micron subset | predictable RAM |
`t

>>Micron coverage
`:tables
`tr38
| Syntax | Example | Support |
|:--|:--:|--:|
| comments/header | source top | yes |
| color headers | top theme | yes |
| headings/anchors | this page | yes |
| section reset | before fields | yes |
| dividers | star/equal bars | yes |
| alignment | l/c/r/a notes | yes |
| literal block | sample | yes |
| reset/escape | inline text | yes |
| links + anchor | nav row | yes |
| table intent | \`tc / \`tr | bounded |
| field variants | below | read-only |
| partial/file | limits | visible fallback |
`t

<
>>Display-only controls
`:controls
Fields render as state chips. They are not submitted by the device browser, so the renderer adds a page notice once.

Callsign: `<18|callsign`TRAIL-MATE>
Role: `<18|role`Gateway scout>
Key: `<!18|token`hidden>
Offline-ready: `<?|offline|yes|*`cache>
Transport: `<^|transport|reticulum|*`Reticulum>

`[`Submit snapshot`/page/index.mu`callsign|offline|transport|*] `[`Anchor hint`/page/index.mu`anchor=matrix]

>>Graceful degradation
`:limits
Unsupported pieces are shown in plain language. This is part of the product promise: when a page asks for more than the device browser can safely do, the user still sees what happened.

`[`Field guide PDF`:/file/trail-mate-field-guide.pdf]

`{:/page/live-status.mu`30}

Unknown Micron token: `q

`tl36
| Fallback | Expected result |
|:--|--:|
| file target | resource unsupported |
| submit fields | no submit |
| partial | unsupported partial |
| unknown tag | counted notice |
`t

-=
`c`[`Top`#top] `[`Overview`#overview] `[`Engine`#engine] `[`Controls`#controls]
