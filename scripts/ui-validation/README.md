# UI validation artifacts

Temporary UI captures belong outside build output:

- PNG screenshots, probes, and contact sheets: `artifacts/ui-validation/screenshots/`
- MKV recordings: `artifacts/ui-validation/recordings/`

Use `Capture-Window.ps1` and `Record-Screen.ps1` for new captures. Other validation
tools should dot-source `ArtifactPaths.ps1` and call `Get-UiValidationArtifactPath`
instead of using `build/x64/Release` as their working output directory.

`Capture-Window.ps1` reads the physical `DWMWA_EXTENDED_FRAME_BOUNDS`, so the PNG
dimensions match the visible HWND bounds rather than the larger invisible resize
border returned by `GetWindowRect`. Pass `-PassThru` to receive the HWND, DPI, and
captured bounds together with the output path. An explicit `-OutputPath` may point
to `docs/images/` for a reviewed formal screenshot; temporary captures must remain
under `artifacts/ui-validation/`.
