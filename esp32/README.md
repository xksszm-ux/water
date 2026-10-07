# ESP32 software scope

This is an unfinished dual-MCU prototype. Hardware acceptance has not passed.
No historical pairing, flashing, or motion statement in older notes is acceptance evidence.

Use the repository root as the single project entry point. ESP-IDF 6.1.x targets classic ESP32.
The top-level CMake configuration rejects other IDF major/minor versions and requires CMake 3.22.
The default firmware remains BLE STOP-only: DRIVE is rejected, and the UART link requires
fresh zero-output STATUS after this handshake's STOP ACK. The extracted
`main/uart/robot_link_safety.c` policy is shared by firmware and host regression checks.

`main/main.c` remains the startup entry. Code is grouped under `main/ble/`
(authorization/lifecycle), `main/uart/` (transport/safety), `main/protocol/`
(wire formats), and `main/self_test/` (bootstrap protocol checks).
These remain sources of the existing main component; no new IDF components were added.

UART2 uses GPIO17 TX / GPIO16 RX at 115200 8N1. Encrypted GATT status remains 20 bytes.
The new V1 type 0x82 diagnostic frame is decoded with `../shared/robot_diagnostics.h`;
latest STM32 heap/stack/heartbeat/error values are printed by app_main, not sent over BLE.
It neither grants ONLINE state nor refreshes motion validity.

V1 type 0x83 carries the latest CAN decision, raw sequence, reject/replay counters
and event age. It reaches app_main logs and the encrypted read-only characteristic
`7e57a000-bbcd-4b20-9f0d-3c8fa62e1003` (18 bytes). Offline or >3000 ms-old reports
are unavailable; a fresh report with no CAN event is distinct. Existing status formats
remain unchanged. See the engineering document for fields, limits and host evidence;
the actual GATT callback, phone read and RX task have not been exercised by host tests.

Build from an ESP-IDF shell with `idf.py build`, or run `../verify.ps1` from the root.
No flash command is part of verify.ps1. A later, explicitly requested device check on
2026-10-05 flashed this v6.1 firmware to COM5 (ESP32-D0WD-V3, 4 MiB), verified writes,
and captured about 45 seconds of boot logs. Protocol self-tests passed, NimBLE reported
advertising, and UART STOP ACK plus fresh zero-output STM32 STATUS kept the link ONLINE.
STM32 continuously reported error=0x5E and invalid distance; peripheral fault causes,
mechanical stopping, phone pairing/encrypted reads, lifecycle failures, resource cleanup,
stack margins and long-term operation remain unverified. Details are recorded only in
engineering section 10; this device observation does not expand the host-test coverage.

On 2026-10-05 the project was rebuilt with the installed official v6.1 SDK and GCC 15.2.0,
using a freshly generated sdkconfig. The 100 Hz RTOS tick is explicit in sdkconfig.defaults;
GATT client is disabled by the disabled Central role, without an invisible explicit setting.
UART V1, diagnostics revision 2, CAN feedback revision 1, BLE packets and STOP-only are unchanged.
Twenty host groups and both MCU builds passed; the app is 503888 bytes in a 1 MiB partition.
Five CMake private-include warnings originate inside IDF's esp_wifi/wpa_supplicant components.
No SDK sources or warning settings were changed. These results do not validate real NimBLE
events or existing-device NVS/bond migration. The additional old/new NimBLE host stack/core
configuration comparison was not completed because its read request was not approved.

See [engineering explanation](../docs/ENGINEERING.md) and [verification evidence](../software_checks/README.md).
