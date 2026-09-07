repo: Losera/incant-audio
branch: main
path: host/Source

## Last sync
date: 2026-09-03T17:56:59Z

### Updated in this project
- Added a shell redesign (2a Command Bar, 2b Rail + Dock) plus both at the 700px minimum, on the unchanged Ember Console tokens.
- Wrote GENERATION_PLAN.md: how to produce generated faces in the current stack, re-grounded against main.
- Confirmed UiIr schema 3 (Theme struct, per-token degradation) has already landed in UiIr.h — earlier Step 1 is done.

## Screen map
| Screen | Built from |
|---|---|
| Incant Audio Shell.dc.html (turn 2 redesign) | host/Source/PluginEditor.h (Chrome, kLeftFraction, kMinWindowW/H, kMaxGridRows), Theme.h, ParamGridPanel.h (kCellH, kHeadingH, kSectionGapH, layoutSectioned) |
| Incant Audio Shell.dc.html (turn 1 recreation) | host/Source/PluginEditor.cpp, PluginEditor.h, Theme.h, ForgeLookAndFeel.h, PromptPanel.cpp/.h, ParamGridPanel.cpp/.h, SampleBrowserPanel.cpp/.h, KeyboardPanel.cpp/.h, CodeEditorPanel.cpp |
| ShellCell / PFCell / PFRotary / PFSection / PFSamples .dc.html | host/Source/ParamGridPanel.cpp (applyPresentation, layoutControls, layoutSectioned, ContentArea::paint), SampleBrowserPanel.cpp |
| Generated Plugin Faces.dc.html | host/Source/UiIr.h, ParamGridPanel.h (derivePalette/deriveTitle/deriveLayoutFromGroups/deriveComponents), docs/ui_design_plan.md |
| ui_ir_system_prompt.md | host/Source/UiIr.h, Theme.h, docs/ui_design_plan.md §3 |
| design_handoff_generated_plugin_faces/GENERATION_PLAN.md | host/Source/UiIr.h, Theme.h, ParamGridPanel.h, PluginEditor.h, llm/CONTRACT.md, llm/recommendation.py, llm/generate.py (process_json_request action dispatch), llm/prompt_builder.py, llm/presentation_block.txt |

## Sync history
- 2026-09-01T18:20:00Z — initial import: shell recreation, four generated-plugin faces, UiIr schema-3 prompt draft.
