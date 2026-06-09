# Cyclone2 D-pad Kill — GameSir Connect patch

Neutralizes the GameSir Cyclone 2 D-pad inside **GameSir Connect** by patching
the app's Electron bundle at the `node-hid` layer. Every HID report the app
reads has the D-pad forced to neutral before the UI sees it; all other inputs
pass through.

This replaces the earlier kernel-driver and XInput-proxy approaches. GameSir
Connect is an Electron app that reads the controller directly through
`node-hid` (`HID.node`), so neither an XInput shim nor a HID/USB driver filter
reaches its input path — but the JavaScript layer does.

## Why node-hid (and why the *unpacked* copy)

GameSir Connect bundles:

```
resources\app.asar              <- packed JS
resources\app.asar.unpacked\node_modules\node-hid\nodehid.js  <- the copy actually loaded
```

`node-hid` is a **native** module, so Electron unpacks the entire node-hid
folder to `app.asar.unpacked` and `require('node-hid')` loads `nodehid.js` from
**there** at runtime. The copy inside `app.asar` is ignored. So the patch
appends the neutralizer directly to the **unpacked** `nodehid.js` — no asar
extract/repack needed in the normal case.

Wrapping the `node-hid` `HID` class is a single chokepoint: every `'data'`
event, `read()/readSync()/readTimeout()`, and `getFeatureReport()` runs through
our scrub, so we never have to find every UI call site.

## What it changes

[tools/gamesir-connect/dpad-neutralizer.js](tools/gamesir-connect/dpad-neutralizer.js)
is appended to the unpacked `nodehid.js`. It forces the D-pad to neutral:

```js
// Vendor report (USB endpoint 0x84): report id 0x12, 64 bytes.
if (data.length >= 59 && data[0] === 0x12) { data[5] = 0x0f; data[58] = 0x0f; }
// Fallback: IG_01 HID report, D-pad hat bits in byte[11] mask 0x1C.
else if (data.length > 11) { data[11] = data[11] & ~0x1c; }
```

The `0x12 / byte[5] / byte[58] / 0x0F` layout is from a USBPcap capture of the
controller's vendor endpoint. If node-hid turns out to read a different report,
run the patch with `-Log` to confirm the real offset (see below).

## Requirements

- Normal case (node-hid unpacked): **none** — direct file edit, no Node needed.
- Fallback case (node-hid packed inside app.asar): Node.js, for
  `npx @electron/asar` repack.

## Patch

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\gamesir-connect\patch-gamesir-connect.ps1
```

Stops GameSir Connect, saves the original `nodehid.js` as `nodehid.js.cyclonebak`
(first run only), restores from it before re-appending (so re-running never
double-applies), then appends the neutralizer to the unpacked `nodehid.js`.

Launch GameSir Connect and test: D-pad reads neutral, everything else normal.

## Confirm the offset (if D-pad still moves)

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\gamesir-connect\patch-gamesir-connect.ps1 -Log
```

Press the D-pad in GameSir Connect, then open the log written to the app's temp
dir (`%TEMP%\gamesir_hid_log.txt`). Find the report bytes that change only on
D-pad presses; send them and the neutralizer offsets can be corrected.

## Restore

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\gamesir-connect\restore-gamesir-connect.ps1
```

Restores the original `app.asar` from the backup.

## Diagnostics

If the patch reports node-hid is missing, or to inspect how the app reads input:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\gamesir-connect\search-app.ps1
```

Extracts `app.asar` and greps for HID / D-pad / controller patterns, writing
hits to the Desktop.

## Scope / limits

- Only affects **GameSir Connect**. Games read the controller through XInput
  (`xusb22`), not this app, so this patch does not change in-game behavior.
- Survives until GameSir Connect updates (an update overwrites `app.asar`).
  Re-run the patch after any update.
- If GameSir Connect ever reads the D-pad through a path other than node-hid
  (e.g. a vendor DLL), this will not cover it; use `-Log` / `search-app.ps1` to
  confirm, then adjust.
