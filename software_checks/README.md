# 软件验证记录

日期：2026-09-15。用户要求先完成软件验证，硬件验收挂起。

## 运行

在工作区根目录的 PowerShell 中执行：

```powershell
& 'C:\Espressif\python_env\idf5.4_py3.11_env\Scripts\python.exe' software_checks/run.py
```

使用本机 MATLAB x64 Clang、STM32 工具包的 lld-link 和 Windows msvcrt；无需安装 Python 包。可通过 `ROBOT_HOST_CLANG` / `ROBOT_HOST_LINK` 指定其他对应工具路径。Codex 沙箱可能需要批准运行链接器。脚本直接编译项目 C 源码并运行，任一编译或检查失败返回非零退出码。中间文件位于 `software_checks/build`；不会打开串口、烧录或复位开发板。

## 已通过

- ESP32 原有 UART 协议自测，失败掩码 0。
- ESP32 原有 BLE 数据协议自测，失败掩码 0。
- ESP32 MOTOR 编码到 STM32 逐字节解析：正负边界、零值、序号与非法 PWM；STM32 ACK 编码到 ESP32 解析。
- 电池保护状态机：启动确认、低压边界、严重低压、恢复滞回、无效数据后的重新确认和低压锁存。
- CAN 控制解码：正负 PWM、错误 DLC、非法 flags、越界 PWM。
- 原有 `BatteryStep7Test_Run`：ADC 换算和电池状态机自测。
- ADC 换算附加测试：零输入、最大采样计数、零参考、非法原始和、空指针，以及失败时清除有效标记。

为运行原有 ADC 自测，将 `BatteryAdc_ConvertSums` 原样移到 `battery_adc_conversion.c`，纯数据定义移到对应头文件；固件和电脑测试共享此实现。没有复制或模拟换算算法。

## 固件构建

| 工程 | 本轮结果 | 占用 |
|---|---|---|
| STM32 Debug | 完整重编译通过，ADC 分离后再次构建通过 | FLASH 57192 B，RAM 16888 B |
| STM32 Release | 完整重编译通过，ADC 分离后再次构建通过 | FLASH 48924 B，RAM 16872 B |
| ESP32 / ESP-IDF 5.4.4 | `idf.py build` 通过（增量检查） | APP 510976 B，1 MiB APP 分区 |

构建未报告编译警告或错误；电脑端 C 检查使用 `-Wall -Wextra -Werror`。本轮未烧录，新生成的 STM32 镜像不能视为已经上板验证。

## 尚未覆盖

本次是现有软件的基线验证，不是全部软件或整机验收通过。未在电脑上执行 FreeRTOS 调度、控制仲裁并发、UART 链路生命周期、NimBLE 安全状态机、DMA 实际采样、PVD 中断、传感器及 Flash I/O、电机驱动时序。此前手机实测证据保留在 ESP32 HANDOFF 中。

硬件供电、分压和 ADC 精度校准、外设及电机实机验收按用户要求挂起。电脑端结果不能证明电路或所有硬件相关执行路径正确。

BLE 非零运动控制、自动避障、OTA、LVGL 等未完成功能不属于本次基线验证成果。继续软件开发时先逐项确定行为、实现并补可运行测试；不以未验收硬件为由自动卡住纯软件工作，也不绕过现有 STOP-only 和电源安全门。
