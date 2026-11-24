# dreo-protocol
ESPhome component for Dreo fans via the wifi module<>MCU serial protocol.

Read https://www.davidc.net/content/hacking-dreo-htf-018s-serial-protocol for a description of this project
and instructions, particularly noting the warning to make sure your fan uses this protocol before flashing it.

Some sample packet captures and a python decoder are in [protocol/](protocol/).

An example full configuration is available in [example-dreo-htf018s-esp32c3.yaml](example-dreo-htf018s-esp32c3.yaml).
This is based on replacing the existing module with an ESP32C3 supermini.

There is also an example configuration for flashing the original MBL01 module in
[example-dreo-htf018s-mbl01.yaml](example-dreo-htf018s-mbl01.yaml).
without even having to open the case. To do this, compile and download a Beken OTA image. Then put the wifi module
into OTA mode (hold the oscillation button for 5 seconds until the countdown ends), connect to the captive access point,
and visit http://192.168.0.1 and upload the file. I have tested this allows me to install a functioning ESPhome
although I haven't tested the fan functionality - although that should be fine 

## Tested models

| Model      | Type  | Module  | Config | Status |
|------------|-------|---------|--------|--------|
| DR-HTF001S | Tower | -       | | Untested, but I believe this is an earlier version of HTF018S. Reports welcome. |
| DR-HTF004S | Tower | MBL02   | | Uses a different protocol, will not work. See [dreo-cloudcutter](https://github.com/ouaibe/dreo-cloudcutter) instead. |
| DR-HTF009  | Tower | MBL01 (original) | [example-dreo-htf009-mbl01.yaml](example-dreo-htf009-mbl01.yaml) | Tested, works. Same datapoints as HTF018S. Flashed over UART, see below. |
| DR-HTF018S | Tower | MBL01 (original) | [example-dreo-htf018s-mbl01.yaml](example-dreo-htf018s-mbl01.yaml) | Tested ESPhome installs okay, not tested fan functionality. Reports welcome. |
| DR-HTF018S | Tower | ESP32C3 | [example-dreo-htf018s-esp32c3.yaml](example-dreo-htf018s-esp32c3.yaml) | Tested, works perfectly. |
| DR-HTF024S | Tower | MBL01 (original) | [example-dreo-htf024s-mbl01.yaml](example-dreo-htf024s-mbl01.yaml) | Untested but [may work](https://github.com/davidc/dreo-protocol/issues/1). |

Please report back successes or failures. Please be careful and always ensure you have a way to restore the original firmware
if needed (e.g. UART), I accept no responsibility for bricked devices!

## Flashing the MBL01 over UART

Tested on a DR-HTF009 (Cruiser Pro T2). The MBL01 sits on the control board in the top cover. Its 4 pads,
counted from the left with the buttons at the top, are 1 TX, 2 RX, 3 GND, 4 3V3.
A 3.3 V USB-UART adapter powers the module alone, the fan MCU stays off until the fan is plugged in.
When ltchiptool waits for the chip, briefly disconnect 3V3 to reset the module into the bootloader.

The Dreo bootloader is not in ltchiptool's list of known bootloaders, so erasing fails with "flash protected".
Running ltchiptool through this small wrapper fixes that:

```python
# ltchiptool-lockfix.py: treat the Dreo bootloader like the known ones that re-protect the flash after each CRC check
import sys
from bk7231tools.serial.cmd_ll_chip import BK7231SerialCmdLLChip
BK7231SerialCmdLLChip.has_crc_flash_protect_lock = property(lambda self: True)
from ltchiptool.__main__ import cli
sys.argv[0] = "ltchiptool"
cli()
```

Back up the original firmware first, then flash ESPHome the same way (`python ltchiptool-lockfix.py flash read ...` / `flash write ...`).
To restore the backup, pass the chip and offsets explicitly, otherwise ltchiptool detects the file as a Realtek image:

```
python ltchiptool-lockfix.py flash write -d /dev/ttyUSB0 -f bk7231n -s 0x11000 -S 0x11000 -l 0x1EF000 backup.bin
```
