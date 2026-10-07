"""Run production C logic on Windows, without boards or extra packages.

Uses PATH/explicit LLVM (legacy local fallback) and Windows msvcrt functions.
Selected HAL/register/OS boundaries are sequential replacements, not real
peripheral timing, RTOS scheduling, BLE security, or motor hardware.
"""
import ctypes as c
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "software_checks/build"
def find_tool(variable, executable, fallback):
    candidate = os.environ.get(variable) or shutil.which(executable) or str(fallback)
    resolved = shutil.which(candidate)
    if resolved is None:
        raise FileNotFoundError(f"{executable} unavailable; add LLVM to PATH or set {variable}")
    return Path(resolved)


CLANG = find_tool("ROBOT_HOST_CLANG", "clang", "C:/Program Files/MATLAB/R2023b/toolbox/sldrt/clang/win64/clang.exe")
LINK = find_tool("ROBOT_HOST_LINK", "lld-link", Path.home() / "AppData/Local/stm32cube/bundles/st-arm-clang/21.1.1+st.7/bin/lld-link.exe")
SERVICE_DIRS = [
    "stm32/Application/Services/Control", "stm32/Application/Services/Protocol",
    "stm32/Application/Services/State", "stm32/Application/Services/Power",
    "stm32/Application/Services/Storage", "stm32/Application/SelfTests",
]
ESP_DIRS = ["esp32/main/ble", "esp32/main/uart", "esp32/main/protocol", "esp32/main/self_test"]


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def check(condition, message):
    if not condition:
        raise AssertionError(message)


BUILD.mkdir(parents=True, exist_ok=True)
# Only declarations: implementations come from Windows, never mocked algorithms.
(BUILD / "string.h").write_text("""#include <stddef.h>
void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
int memcmp(const void *, const void *, size_t);
size_t strlen(const char *);
""", encoding="utf-8")
(BUILD / "crt.def").write_text("LIBRARY msvcrt.dll\nEXPORTS\nmemcpy\nmemmove\nmemset\nmemcmp\nstrlen\n", encoding="utf-8")
run(LINK, "/lib", "/machine:x64", f"/def:{BUILD / 'crt.def'}", f"/out:{BUILD / 'crt.lib'}")
sources = [
    "esp32/main/protocol/robot_protocol.c", "esp32/main/self_test/robot_protocol_self_test.c",
    "esp32/main/protocol/robot_ble_protocol.c", "esp32/main/self_test/robot_ble_protocol_self_test.c",
    "stm32/Application/Services/Protocol/protocol.c", "stm32/Application/Services/Power/battery_monitor.c",
    "stm32/Drivers/CAN/can_protocol.c",
    "stm32/Drivers/ADC/battery_adc_conversion.c", "stm32/Application/SelfTests/battery_step7_test.c",
    "stm32/Application/Services/State/robot_state.c",
    "stm32/Application/Tasks/motor_task.c", "software_checks/logic/control_checks.c",
    "esp32/main/uart/robot_link_safety.c", "software_checks/logic/link_checks.c", "software_checks/protocol/wire_checks.c", "software_checks/integration/communication_checks.c",
    "esp32/main/ble/robot_ble_lifecycle.c", "software_checks/logic/ble_lifecycle_checks.c",
    "stm32/Application/Tasks/task_health.c", "software_checks/logic/task_health_checks.c",
    "software_checks/startup/startup_checks.c",
]
exports = ["RobotProtocolSelfTest_Run", "RobotBleProtocolSelfTest_Run",
           "ProtocolParser_Init", "ProtocolParser_PushByte", "ProtocolParser_Next",
           "Protocol_DecodeMotor", "RobotProtocol_EncodeMotor", "Protocol_EncodeAck",
           "RobotProtocolParser_Init", "RobotProtocolParser_PushByte", "RobotProtocolParser_Next",
           "RobotProtocol_DecodeAck", "BatteryMonitor_Init", "BatteryMonitor_Update",
           "BatteryMonitor_Invalidate", "BatteryMonitor_GetState", "CanProtocol_DecodeControl",
           "BatteryStep7Test_Run", "BatteryAdc_ConvertSums", "ControlChecks_Run", "LinkChecks_Run", "WireChecks_Run", "CommunicationChecks_Run", "CrossMcuScenario_Run", "BleLifecycleChecks_Run", "TaskHealthChecks_Run", "StartupChecks_Run"]
objects = []
for index, source in enumerate(sources):
    obj = BUILD / f"{index}.obj"
    run(CLANG, "--target=x86_64-pc-windows-msvc", "-ffreestanding", "-fno-stack-protector",
        "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", BUILD,
        *(["-I", ROOT / "software_checks/startup_stubs"] if source == "software_checks/startup/startup_checks.c" else []),
        "-I", ROOT / "shared", "-I", ROOT / "software_checks/stubs", "-I", ROOT / "stm32/Application/Tasks",
        "-I", ROOT / "stm32/Drivers/UART", "-I", ROOT / "stm32/Drivers/Motor",
        *[arg for path in ESP_DIRS + SERVICE_DIRS for arg in ("-I", ROOT / path)],
        "-I", ROOT / "stm32/Drivers/CAN", "-I", ROOT / "stm32/Drivers/ADC",
        "-c", ROOT / source, "-o", obj)
    objects.append(obj)
run(LINK, "/dll", "/noentry", "/machine:x64", f"/out:{BUILD / 'checks.dll'}",
    *[f"/export:{name}" for name in exports], *objects, BUILD / "crt.lib")

# Keep this init check in its own DLL: other checks replace AppTasks' runtime
# functions, while this one must compile the production AppTasks_Init itself.
init_obj = BUILD / "app_tasks_init.obj"
run(CLANG, "--target=x86_64-pc-windows-msvc", "-ffreestanding", "-fno-stack-protector",
    "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", ROOT / "software_checks/startup_stubs",
    "-I", ROOT / "shared",
    "-I", ROOT / "stm32/Application/Tasks",
    *[arg for path in SERVICE_DIRS for arg in ("-I", ROOT / path)],
    "-I", ROOT / "stm32/Drivers/Motor",
    "-c", ROOT / "software_checks/startup/app_tasks_init_checks.c", "-o", init_obj)
run(LINK, "/dll", "/noentry", "/machine:x64",
    f"/out:{BUILD / 'app_tasks_init.dll'}", "/export:AppTasksInitChecks_Run",
    init_obj, BUILD / "crt.lib")


def build_boundary_checks(name, sources, stub_dir, include_dirs, exports):
    objects = []
    for index, source in enumerate(sources):
        obj = BUILD / f"{name}_{index}.obj"
        run(CLANG, "--target=x86_64-pc-windows-msvc", "-ffreestanding", "-fno-stack-protector",
            "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", BUILD,
            "-I", ROOT / stub_dir, *[arg for path in include_dirs for arg in ("-I", ROOT / path)],
            "-c", ROOT / source, "-o", obj)
        objects.append(obj)
    run(LINK, "/dll", "/noentry", "/machine:x64", f"/out:{BUILD / (name + '.dll')}",
        *[f"/export:{export}" for export in exports], *objects, BUILD / "crt.lib")
    return BUILD / (name + '.dll')


driver_dll = build_boundary_checks("drivers",
    ["software_checks/drivers/uart_driver_checks.c", "software_checks/drivers/can_driver_checks.c",
     "software_checks/drivers/storage_log_checks.c", "stm32/Drivers/CAN/can_protocol.c"], "software_checks/driver_stubs",
    ["software_checks/stubs", "stm32/Drivers/UART", "stm32/Drivers/CAN", "stm32/Drivers/W25Q64", *SERVICE_DIRS],
    ["UartDriverChecks_Run", "CanDriverChecks_Run", "StorageLogChecks_Run"])

peripheral_checks = []
for name, sources, includes, export in (
    ("power_motor", ["software_checks/drivers/power_motor_checks.c"],
     ["stm32/Drivers/Motor", "stm32/Application/Services/Power"], "PowerMotorChecks_Run"),
    ("adc_dma", ["software_checks/drivers/adc_dma_checks.c", "stm32/Drivers/ADC/battery_adc_conversion.c"],
     ["stm32/Drivers/ADC"], "AdcDmaChecks_Run"),
    ("ultrasonic", ["software_checks/drivers/ultrasonic_checks.c"],
     ["stm32/Drivers/HC_SR04"], "UltrasonicChecks_Run"),
    ("i2c_spi", ["software_checks/drivers/i2c_spi_checks.c"],
     ["stm32/Drivers/MPU6050", "stm32/Drivers/OLED", "stm32/Drivers/W25Q64"], "I2cSpiChecks_Run"),
):
    peripheral_checks.append((build_boundary_checks(name, sources,
        "software_checks/peripheral_stubs", includes, [export]), export))

# The declarations live in one reviewed header. Empty SDK include shims are
# generated here so the test can include the complete production UART source.
esp_stubs = BUILD / "esp_uart_stubs"
for header in ("esp_err.h", "esp_log.h", "inttypes.h", "driver/gpio.h", "driver/uart.h",
               "freertos/FreeRTOS.h", "freertos/queue.h", "freertos/task.h"):
    path = esp_stubs / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("/* APIs declared by esp_uart_start_stubs.h. */\n", encoding="utf-8")
esp_start_dll = build_boundary_checks("esp_uart_start",
    ["software_checks/startup/esp_uart_start_checks.c", "esp32/main/protocol/robot_protocol.c",
     "esp32/main/uart/robot_link_safety.c"], str(esp_stubs),
    ["shared", *ESP_DIRS], ["EspUartStartChecks_Run"])

ble_stubs = BUILD / "esp_ble_stubs"
for header in ("esp_err.h", "esp_log.h", "driver/gpio.h", "freertos/FreeRTOS.h",
               "freertos/event_groups.h", "freertos/task.h", "host/ble_att.h", "host/ble_gap.h",
               "host/ble_gatt.h", "host/ble_hs.h", "host/ble_hs_pvcy.h", "host/ble_store.h",
               "host/ble_uuid.h", "nimble/nimble_port.h", "nvs_flash.h", "os/os_mbuf.h",
               "services/gap/ble_svc_gap.h", "services/gatt/ble_svc_gatt.h"):
    path = ble_stubs / header
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("/* APIs declared by esp_ble_stubs.h. */\n", encoding="utf-8")
peripheral_checks.append((build_boundary_checks("esp_ble",
    ["software_checks/startup/esp_ble_checks.c", "esp32/main/ble/robot_ble_lifecycle.c",
     "esp32/main/protocol/robot_ble_protocol.c", "esp32/main/protocol/robot_protocol.c"],
    str(ble_stubs), ["shared", *ESP_DIRS], ["EspBleChecks_Run"]), "EspBleChecks_Run"))

# Finish every compile/link before loading checks. A host execution-policy
# failure must not leave later production sources uncompiled.
print("All software check DLLs compiled with -Wall -Wextra -Werror.")
dll = c.CDLL(str(BUILD / "checks.dll"))
init_dll = c.CDLL(str(BUILD / "app_tasks_init.dll"))
driver_dll = c.CDLL(str(driver_dll))
esp_start_dll = c.CDLL(str(esp_start_dll))
peripheral_checks = [(c.CDLL(str(path)), name) for path, name in peripheral_checks]


def fn(name, result, *args):
    f = getattr(dll, name)
    f.restype, f.argtypes = result, args
    return f


for name in exports[:2]:
    mask = c.c_uint32()
    check(fn(name, c.c_bool, c.POINTER(c.c_uint32))(c.byref(mask)), f"{name}: mask={mask.value:#x}")
    print(f"PASS {name}: mask={mask.value:#x}")


class Parser(c.Structure):
    _fields_ = [("bytes", c.c_uint8 * 96), ("count", c.c_uint16), ("time", c.c_uint32)]


class Frame(c.Structure):
    _fields_ = [("version", c.c_uint8), ("type", c.c_uint8), ("flags", c.c_uint8),
                ("sequence", c.c_uint16), ("length", c.c_uint8), ("payload", c.c_uint8 * 32)]


class Motor(c.Structure):
    _fields_ = [("left", c.c_int16), ("right", c.c_int16), ("flags", c.c_uint8)]


def parse(prefix, wire):
    parser, frame = Parser(), Frame()
    fn(prefix + "Parser_Init", None, c.POINTER(Parser))(c.byref(parser))
    push = fn(prefix + "Parser_PushByte", c.c_bool, c.POINTER(Parser), c.c_uint8, c.c_uint32)
    take = fn(prefix + "Parser_Next", c.c_int, c.POINTER(Parser), c.c_uint32, c.POINTER(Frame))
    for i, byte in enumerate(wire):
        check(push(c.byref(parser), byte, i), "parser overflow")
        result = take(c.byref(parser), i, c.byref(frame))
        check(result == (1 if i == len(wire) - 1 else 0), f"fragment parse at byte {i}: {result}")
    return frame


wire = (c.c_uint8 * 42)()
encode = fn("RobotProtocol_EncodeMotor", c.c_size_t, c.c_uint16, c.c_int16, c.c_int16,
            c.c_bool, c.POINTER(c.c_uint8), c.c_size_t)
decode = fn("Protocol_DecodeMotor", c.c_bool, c.POINTER(Frame), c.POINTER(Motor))
for left in (-1000, -1, 0, 1, 1000):
    n = encode(65535, left, -left, True, wire, len(wire))
    check(n == 15, "motor frame length")
    frame = parse("Protocol", wire[:n])
    command = Motor()
    check(decode(c.byref(frame), c.byref(command)), "STM32 rejected ESP32 frame")
    check((command.left, command.right, frame.sequence) == (left, -left, 65535), "motor fields differ")
check(encode(0, 1001, 0, True, wire, len(wire)) == 0, "out-of-range motor accepted")
n = fn("Protocol_EncodeAck", c.c_size_t, c.c_uint16, c.c_uint8, c.c_uint8,
       c.c_uint8, c.c_uint8, c.POINTER(c.c_uint8), c.c_size_t)(42, 2, 0, 0, 255, wire, len(wire))
frame = parse("RobotProtocol", wire[:n])
class Ack(c.Structure):
    _fields_ = [("sequence", c.c_uint16), ("type", c.c_uint8), ("result", c.c_uint8),
                ("mode", c.c_uint8), ("owner", c.c_uint8)]
ack = Ack()
check(fn("RobotProtocol_DecodeAck", c.c_bool, c.POINTER(Frame), c.POINTER(Ack))(c.byref(frame), c.byref(ack)), "ESP32 ACK decode")
check((ack.sequence, ack.type, ack.result, ack.owner) == (42, 2, 0, 255), "ACK fields differ")
print("PASS STM32/ESP32 real-code interoperability: fragmented MOTOR boundary values and ACK")

init = fn("BatteryMonitor_Init", None)
update = fn("BatteryMonitor_Update", c.c_int, c.c_uint16)
invalidate = fn("BatteryMonitor_Invalidate", None)
state = fn("BatteryMonitor_GetState", c.c_int)
init()
for voltage, expected in [(3801, 0), (3801, 1), (3800, 1), (3800, 2),
                          (4000, 2), (3999, 2), (4000, 2), (4000, 2), (4000, 1), (3500, 2)]:
    check(update(voltage) == expected, f"battery state at {voltage}")
invalidate()
check(state() == 2, "invalid data cleared low latch")
init()
check(update(4500) == 0, "startup qualification")
invalidate()
check(update(4500) == 0 and update(4500) == 1, "requalification")
print("PASS battery state machine: qualification, threshold boundaries, hysteresis, invalidation")

class CanMotor(c.Structure):
    _fields_ = [("left", c.c_int16), ("right", c.c_int16), ("mode", c.c_int),
                ("enable", c.c_bool), ("sequence", c.c_uint8)]
can_decode = fn("CanProtocol_DecodeControl", c.c_int, c.POINTER(c.c_uint8), c.c_uint8, c.POINTER(CanMotor))
data = (c.c_uint8 * 8)(0xE8, 3, 0x18, 0xFC, 1, 1, 255, 1)
command = CanMotor()
check(can_decode(data, 8, c.byref(command)) == 0 and command.left == 1000 and command.right == -1000, "CAN boundaries")
check(can_decode(data, 7, c.byref(command)) != 0, "CAN bad DLC")
data[5] = 2
check(can_decode(data, 8, c.byref(command)) != 0, "CAN bad flags")
data[5], data[0] = 1, 0xE9
check(can_decode(data, 8, c.byref(command)) != 0, "CAN PWM range")
print("PASS CAN control decoder: signed boundaries, bad DLC/flags/range")
check(fn("BatteryStep7Test_Run", c.c_bool)(), "ADC and battery startup self-test")
print("PASS BatteryStep7Test_Run: original ADC conversion and battery protection tests")

class Reading(c.Structure):
    _fields_ = [("raw", c.c_uint16), ("vref_raw", c.c_uint16), ("vdda", c.c_uint16),
                ("voltage", c.c_uint16), ("safety", c.c_uint16), ("valid", c.c_bool)]
convert = fn("BatteryAdc_ConvertSums", c.c_bool, c.c_uint32, c.c_uint32, c.c_uint16, c.POINTER(Reading))
reading = Reading()
for battery, vref, count in [(0, 1489, 1), (2792*8, 1489*8, 8), (2792*65535, 1489*65535, 65535)]:
    check(convert(battery, vref, count, c.byref(reading)), "valid ADC sums rejected")
    check(reading.valid and reading.safety <= reading.voltage, "ADC validity or conservative voltage")
for battery, vref, count in [(0, 0, 0), (1, 0, 1), (4096, 1489, 1),
                              (2792, 4096, 1), (4079, 1489, 1), (2792, 1, 1)]:
    reading.valid = True
    check(not convert(battery, vref, count, c.byref(reading)) and not reading.valid,
          "invalid ADC sums retained valid output")
check(not convert(2792, 1489, 1, None), "ADC null output accepted")
print("PASS ADC conversion: zero input, maximum count, invalid sums/reference, output invalidation")
line = fn("ControlChecks_Run", c.c_int)()
check(line == 0, f"control_checks.c:{line}: production control regression failed")
print("PASS production arbiter/state/motor task: STOP, timeout, ownership, wrap and invalidation")
line = fn("LinkChecks_Run", c.c_int)()
check(line == 0, f"link_checks.c:{line}: safety handshake regression failed")
print("PASS STOP-only link policy: ACK, post-ACK status, stale/nonzero, reconnect and wrap")
line = fn("WireChecks_Run", c.c_int)()
check(line == 0, f"wire_checks.c:{line}: cross-MCU wire regression failed")
print("PASS diagnostics/status/BLE interoperability and STM32 parser recovery")
line = fn("CommunicationChecks_Run", c.c_int)()
check(line == 0, f"communication_checks.c:{line}: dispatcher regression failed")
print("PASS production UART dispatcher: expired fragments, bounded responses, diagnostics and stale export")
line = fn("CrossMcuScenario_Run", c.c_int)()
check(line == 0, f"communication_checks.c:{line}: cross-MCU scenario failed")
print("PASS cross-MCU scenario: STOP handshake, lost ACK, diagnostics, CRC, full queue, motion isolation, stale status, recovery, wrap and CAN/AUTO safety rejection")
line = fn("BleLifecycleChecks_Run", c.c_int)()
check(line == 0, f"ble_lifecycle_checks.c:{line}: BLE lifecycle decision failed")
print("PASS BLE lifecycle decisions: disconnect retry deadline, pairing reset gate and tick wrap")
line = fn("TaskHealthChecks_Run", c.c_int)()
check(line == 0, f"task_health_checks.c:{line}: task health decision failed")
print("PASS STM32 task health policy: per-task deadlines, startup grace, latched fault and wraps")
line = fn("StartupChecks_Run", c.c_int)()
check(line == 0, f"startup_checks.c:{line}: bootstrap failure decision failed")
print("PASS STM32 bootstrap: injected kernel-not-ready and task-creation failures reach Error_Handler")
init_check = init_dll.AppTasksInitChecks_Run
init_check.restype = c.c_int
line = init_check()
check(line == 0, f"app_tasks_init_checks.c:{line}: partial creation failure was accepted")
print("PASS STM32 AppTasks_Init: injected queue, mutex and partial task creation failures")
for name in ("UartDriverChecks_Run", "CanDriverChecks_Run", "StorageLogChecks_Run"):
    run_check = getattr(driver_dll, name)
    run_check.restype = c.c_int
    line = run_check()
    check(line == 0, f"{name}: source line {line}: driver boundary check failed")
    print(f"PASS {name}: production logic with peripheral boundary replacements")
esp_start_check = esp_start_dll.EspUartStartChecks_Run
esp_start_check.restype = c.c_int
line = esp_start_check()
check(line == 0, f"esp_uart_start_checks.c:{line}: partial UART startup failed")
print("PASS ESP32 UART startup: nine creation/configuration failures, cleanup, retry and duplicate start")
for boundary_dll, name in peripheral_checks:
    run_check = getattr(boundary_dll, name)
    run_check.restype = c.c_int
    line = run_check()
    check(line == 0, f"{name}: source line {line}: peripheral boundary check failed")
    print(f"PASS {name}: production logic with peripheral boundary replacements")
print("Software checks passed. Electrical behavior and real RTOS/NimBLE scheduling are NOT covered.")
