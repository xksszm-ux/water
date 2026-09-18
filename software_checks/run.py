"""Run production pure-C logic on Windows, without boards or extra packages.

Uses the installed MATLAB Clang/ST linker and Windows msvcrt memory functions.
Does not emulate peripherals, RTOS scheduling, BLE security, or motor hardware.
"""
import ctypes as c
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "software_checks/build"
CLANG = Path(os.environ.get("ROBOT_HOST_CLANG", "C:/Program Files/MATLAB/R2023b/toolbox/sldrt/clang/win64/clang.exe"))
LINK = Path(os.environ.get("ROBOT_HOST_LINK", str(Path.home() / "AppData/Local/stm32cube/bundles/st-arm-clang/21.1.1+st.7/bin/lld-link.exe")))


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
""", encoding="utf-8")
(BUILD / "crt.def").write_text("LIBRARY msvcrt.dll\nEXPORTS\nmemcpy\nmemmove\nmemset\nmemcmp\n", encoding="utf-8")
run(LINK, "/lib", "/machine:x64", f"/def:{BUILD / 'crt.def'}", f"/out:{BUILD / 'crt.lib'}")
sources = [
    "esp32/main/robot_protocol.c", "esp32/main/robot_protocol_self_test.c",
    "esp32/main/robot_ble_protocol.c", "esp32/main/robot_ble_protocol_self_test.c",
    "stm32/Application/Services/protocol.c", "stm32/Application/Services/battery_monitor.c",
    "stm32/Drivers/CAN/can_protocol.c",
    "stm32/Drivers/ADC/battery_adc_conversion.c", "stm32/Application/Services/battery_step7_test.c",
]
exports = ["RobotProtocolSelfTest_Run", "RobotBleProtocolSelfTest_Run",
           "ProtocolParser_Init", "ProtocolParser_PushByte", "ProtocolParser_Next",
           "Protocol_DecodeMotor", "RobotProtocol_EncodeMotor", "Protocol_EncodeAck",
           "RobotProtocolParser_Init", "RobotProtocolParser_PushByte", "RobotProtocolParser_Next",
           "RobotProtocol_DecodeAck", "BatteryMonitor_Init", "BatteryMonitor_Update",
           "BatteryMonitor_Invalidate", "BatteryMonitor_GetState", "CanProtocol_DecodeControl",
           "BatteryStep7Test_Run", "BatteryAdc_ConvertSums"]
objects = []
for index, source in enumerate(sources):
    obj = BUILD / f"{index}.obj"
    run(CLANG, "--target=x86_64-pc-windows-msvc", "-ffreestanding", "-fno-stack-protector",
        "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror", "-I", BUILD,
        "-I", ROOT / "esp32/main", "-I", ROOT / "stm32/Application/Services",
        "-I", ROOT / "stm32/Drivers/CAN", "-I", ROOT / "stm32/Drivers/ADC",
        "-c", ROOT / source, "-o", obj)
    objects.append(obj)
run(LINK, "/dll", "/noentry", "/machine:x64", f"/out:{BUILD / 'checks.dll'}",
    *[f"/export:{name}" for name in exports], *objects, BUILD / "crt.lib")
dll = c.CDLL(str(BUILD / "checks.dll"))


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
print("Software checks passed. Hardware/RTOS/BLE lifecycle and ADC acquisition are NOT covered.")
