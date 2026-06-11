# Arduino IDE Setup

## Prerequisites

### 1. Rosetta 2 (Apple Silicon Macs only)
megaTinyCore's avr-gcc toolchain is x86_64 only. Install Rosetta before anything else:
```bash
softwareupdate --install-rosetta --agree-to-license
```

### 2. megaTinyCore
In Arduino IDE: **File → Preferences → Additional boards manager URLs**, add:
```
http://drazzy.com/package_drazzy.com_index.json
```
Then **Tools → Board → Boards Manager**, search `megaTinyCore`, install.

## Required settings

Open **Tools** menu and set exactly:

| Setting | Value |
|---|---|
| Board | `ATtiny3216/1616/1606/816/806/416/406` (**NOT** the "w/Optiboot" variant) |
| Chip | `ATtiny1616` |
| Clock | `5 MHz internal tuned` |
| Programmer | `SerialUPDI - 230400 baud` |
| Port | Your UPDI Friend's port (e.g. `/dev/cu.usbserial-XXXX`) |

**After changing Board, always re-check Chip** — it resets to the default for that family.

## Uploading

Use **Sketch → Upload Using Programmer** (or Shift+Cmd+U).  
Do NOT use the plain Upload button — that expects a bootloader which isn't on the chip.

## Verifying the port

With the UPDI Friend connected, check **Tools → Port**. You should see something like
`/dev/cu.usbserial-XXXX`. If nothing appears, try toggling the 3V/5V switch on the
UPDI Friend, then unplug/replug.

## FQBN (for CLI use)
```
megaTinyCore:megaavr:atxy6:chip=1616,clock=5internaltuned
```
