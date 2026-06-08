# HID Report Logger

Use this helper to find the HID report values needed by `Cyclone2DpadFilter.inf`:

- `ReportId`
- `DpadByteOffset`
- `DpadMask`
- `DpadNeutralValue`

## Install dependency

Open Command Prompt or PowerShell:

```cmd
py -m pip install hidapi
```

## Run

```cmd
py dump_hid_reports.py
```

Select the GameSir Cyclone 2 HID device from the list.

## Capture sequence

Do this slowly and press only one control at a time:

1. Leave the controller untouched and copy the neutral report.
2. Press D-pad Up only and copy the report.
3. Release.
4. Press D-pad Right only and copy the report.
5. Release.
6. Press D-pad Down only and copy the report.
7. Release.
8. Press D-pad Left only and copy the report.
9. Release.

## What to send for analysis

Paste the output in this format:

```text
USB or Bluetooth:
Controller mode:
Selected HID device index:
VID/PID:

Neutral:

D-pad Up:

D-pad Right:

D-pad Down:

D-pad Left:
```

## How to interpret common patterns

### Hat switch nibble with report ID

```text
Neutral: 01 80 80 08 00 00
Up:      01 80 80 00 00 00
Right:   01 80 80 02 00 00
Down:    01 80 80 04 00 00
Left:    01 80 80 06 00 00
```

Values:

```text
ReportId = 1
DpadByteOffset = 3
DpadMask = 0x0F
DpadNeutralValue = 0x08
```

### Hat switch nibble without report ID

```text
Neutral: 80 80 08 00 00
Up:      80 80 00 00 00
Right:   80 80 02 00 00
Down:    80 80 04 00 00
Left:    80 80 06 00 00
```

Values:

```text
ReportId = 0
DpadByteOffset = 2
DpadMask = 0x0F
DpadNeutralValue = 0x08
```

### Four separate button bits

```text
Neutral: 01 80 80 00 00
Up:      01 80 80 01 00
Down:    01 80 80 02 00
Left:    01 80 80 04 00
Right:   01 80 80 08 00
```

Values:

```text
ReportId = 1
DpadByteOffset = 3
DpadMask = 0x0F
DpadNeutralValue = 0x00
```
