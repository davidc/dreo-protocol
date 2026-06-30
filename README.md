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
| DR-HTF018S | Tower | MBL01 (original) | [example-dreo-htf018s-mbl01.yaml](example-dreo-htf018s-mbl01.yaml) | Tested ESPhome installs okay, not tested fan functionality. Reports welcome. |
| DR-HTF018S | Tower | ESP32C3 | [example-dreo-htf018s-esp32c3.yaml](example-dreo-htf018s-esp32c3.yaml) | Tested, works perfectly. |
| DR-HTF024S | Tower | MBL01??? | | Untested but [may work](https://github.com/ouaibe/dreo-cloudcutter/issues/14). |

Please report back successes or failures. Please be careful and always ensure you have a way to restore the original firmware
if needed (e.g. UART), I accept no responsibility for bricked devices!
