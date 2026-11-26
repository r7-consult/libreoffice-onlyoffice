# Results: prototype P-1 (option A+)

Status key: PASS = measured and within the threshold; FAIL = measured and not within it; NOT VERIFIED = not measured on this machine. No row may say PASS without a measurement. Nothing has been run yet (draft; see `PROGRESS.md`).

| # | Criterion (plan P-1, section 11) | Threshold | Status | Measurement and log | LibreOffice | Date |
|---|---|---|---|---|---|---|
| 1 | Plugin shown in the sidebar on Windows, macOS and Linux (X11 and XWayland) | shown on all three | NOT VERIFIED (Windows draft only) | | | |
| 2 | Switching tabs, context change, undocking: no plugin restart | `webhost|created` once | NOT VERIFIED | stage 3 test | | |
| 3 | `callCommand` writes 10 000 cells; one Undo reverts it | <= 1000 ms; A1 empty after Undo | NOT VERIFIED | stage 5 test (`fill_ms`) | | |
| 4 | Infinite loop stopped by the watchdog; LibreOffice still works | TimeoutError; UNO answers | NOT VERIFIED | stage 5 test | | |
| 5 | Browser process failure does not stop LibreOffice; plugin restarts | alive; `count=2` after restart | NOT VERIFIED | stage 7 test | | |
| 6 | One OXT works on two LibreOffice releases without rebuilding | stage 1-7 tests pass on A and B | NOT VERIFIED | `run_all.ps1` | | |
| 7 | Plugin menu item appears with no visible delay | <= 300 ms (median of 20) | NOT VERIFIED | manual, see TEST_MATRIX | | |
| 8 | No GTK conflict on Linux with the gtk3 VCL plugin | no `Gtk-ERROR` | NOT VERIFIED (Linux not built) | | | |

## Additional measurements (to fill in)
- Memory per panel (WebView2 `WorkingSet`): NOT MEASURED
- Cold start of the plugin (`created` to `navigated`): NOT MEASURED
- High DPI, IME, screen reader: NOT MEASURED

## Decision (plan section 12)
Not decided. The decision is recorded as ADR-001 by a person after the runs above.
