# UI validation artifacts

Temporary UI captures belong outside build output:

- PNG screenshots, probes, and contact sheets: `artifacts/ui-validation/screenshots/`
- MKV recordings: `artifacts/ui-validation/recordings/`

Use `Capture-Window.ps1` and `Record-Screen.ps1` for new captures. Other validation
tools should dot-source `ArtifactPaths.ps1` and call `Get-UiValidationArtifactPath`
instead of using `build/x64/Release` as their working output directory.
