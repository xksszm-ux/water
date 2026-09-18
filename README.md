# STM32-ESP32 FreeRTOS Robot

基于 STM32F103C8T6 与经典 ESP32 的双主控移动机器人项目。STM32 负责实时控制、传感采集和安全停车，ESP32 负责 BLE 通信，两板通过自定义 UART V1 协议交换命令与状态。

> 当前状态：STM32/ESP32 软件构建和电脑端协议检查通过，BLE 的配对、加密、状态通知及安全 STOP 已完成实机联调。BLE 仍为 STOP-only，供电、传感器、四电机及整车运动尚未完成最终硬件验收。请勿将“构建通过”理解为“整车已验证”。

## 系统架构

```text
手机 / nRF Connect
        │ BLE GATT（加密读写、状态通知、STOP-only）
        ▼
      ESP32
        │ UART2 115200 8N1
        │ A5 5A + 版本/类型/标志/序号/长度/载荷 + CRC16
        ▼
 STM32F103C8T6 + FreeRTOS
   ├─ MotorTask         TB6612 左右两路开环 PWM
   ├─ SensorTask        MPU6050、HC-SR04
   ├─ BatteryTask       ADC + VREFINT + 低压状态机
   ├─ CommunicationTask UART DMA/IDLE + 协议解析
   ├─ CanTask           CAN 控制/状态
   ├─ DisplayTask       SSD1306 OLED
   └─ StorageTask       W25Q64 日志
```

## 主要实现

### STM32

- 7 个静态 FreeRTOS 任务；单槽最新值队列传递控制和传感数据，互斥锁保护 MPU6050 与 OLED 共用的 I2C1。
- AppTasks_GetDiagnostics() 汇总 7 个任务心跳、各任务栈高水位、当前/历史最小空闲堆及队列丢包计数，供上板长稳测试通过调试器周期采样。
- USART1 使用 64 B 循环 DMA + UART IDLE；ISR 仅搬运字节并通知任务，任务从 256 B 环形缓冲区解析协议。
- UART V1 支持断帧、粘包、噪声、坏 CRC 重同步、50 ms 半帧超时、ACK 和周期状态上传。
- CAN/UART 控制源所有权、独立序号窗口、防重放、STOP 优先和模式限制。
- 电机软件安全链：300 ms 命令超时、换向死区、20% 恢复限幅、低压/ADC 无效或过期停车、PVD 欠压紧急停车。
- W25Q64 日志包含版本、边界检查、写后读回和 CRC；HC-SR04 采用非阻塞边沿捕获与硬件超时。

### ESP32

- ESP-IDF 5.4.4、NimBLE、UART2（GPIO17 TX / GPIO16 RX）。
- 单连接 BLE GATT Server：加密命令写入、加密状态读取和约 200 ms 状态通知。
- Secure Connections Just Works、绑定、RPA 隐私、单绑定及 BOOT/GPIO0 触发的 60 s 换绑窗口。
- 连接、断连、取消订阅、加密失败或 NimBLE Host reset 均请求 UART 安全 STOP。
- 链路只有在收到本轮 STOP ACK，且随后收到左右实际 PWM 都为 0 的新鲜状态后才进入 ONLINE。
- 当前固件故意拒绝所有 DRIVE 命令，不存在 BLE 非零 PWM 路径。

## UART V1

```text
A5 5A | version | type | flags | sequence_u16_le | length | payload | crc16_le
```

- 最大载荷：32 B；最大帧长：42 B。
- CRC：CRC-16/CCITT-FALSE。
- 消息：MOTOR、STOP、SET_MODE、STATUS_REQUEST、HEARTBEAT、ACK、STATUS。
- UART IDLE 只是唤醒条件，不作为帧边界。

完整字段和状态机见 [`stm32/docs/STEP9_UART_PROTOCOL.md`](stm32/docs/STEP9_UART_PROTOCOL.md) 与 [`esp32/docs/STEP10_BLE_PROTOCOL.md`](esp32/docs/STEP10_BLE_PROTOCOL.md)。

## 仓库结构

```text
stm32/            STM32CubeMX/HAL/CMake/FreeRTOS 工程
esp32/            ESP-IDF/NimBLE 工程
software_checks/  直接编译生产 C 源码的电脑端检查
```

仓库不包含构建目录、固件镜像、ESP-IDF 生成的 `sdkconfig` 或本机工具配置。

## 构建

### STM32

需要 CMake 3.22+、Ninja 与 ARM GNU Toolchain：

```powershell
cd stm32
cmake --preset Release
cmake --build --preset Release
```

已记录的构建结果：

| 配置 | FLASH | RAM |
|---|---:|---:|
| Debug | 57,192 B / 64 KiB | 16,888 B / 20 KiB |
| Release | 48,924 B / 64 KiB | 16,872 B / 20 KiB |

### ESP32

需要 ESP-IDF 5.4.4：

```powershell
cd esp32
idf.py set-target esp32
idf.py build
```

`sdkconfig.defaults` 保存 BLE/NimBLE 所需的可复现配置；本机生成的 `sdkconfig` 不提交。

### 电脑端软件检查

Windows 检查脚本会直接编译项目中的协议、电池和 CAN 生产源码，不模拟 RTOS、BLE 生命周期或硬件：

```powershell
python software_checks/run.py
```

如默认工具路径不适用，可设置 `ROBOT_HOST_CLANG` 与 `ROBOT_HOST_LINK`。

当前通过的 7 组检查包括：ESP32 UART/BLE 协议自测、STM32/ESP32 协议互通、电池状态机、CAN 控制解码、ADC 原有自测及 ADC 边界测试。

## 已验证与未验证

| 范围 | 状态 |
|---|---|
| STM32 Debug/Release、ESP32 构建 | 已通过 |
| UART/BLE 协议与双板协议互通检查 | 已通过 |
| 手机扫描、配对、加密读取、通知、STOP、断连/重连 | 已实测 |
| 坏 CRC、错误长度、合法 DRIVE 拒绝 | 已实测 |
| FreeRTOS 心跳/栈水位/最小空闲堆采集接口 | 已实现，长期运行数据待上板采集 |
| 电池分压与阈值校准、PVD 实机响应 | 待测 |
| OLED、MPU6050、W25Q64、HC-SR04 完整实机回归 | 待测 |
| TB6612、四电机与整车运动 | 待测 |
| CAN 收发器、终端电阻和第二节点 | 待测 |

## 安全说明

- 当前 BLE 只允许 STOP 与状态读取；请勿绕过限制直接开放 DRIVE。
- HC-SR04 Echo 必须经过分压或电平转换后接入 STM32。
- STM32、ESP32 与外设必须共地，但 4.5 V、5 V 和 3.3 V 电源正极不能混接。
- V1 使用无编码器 TT 电机，只有左右两路开环 PWM；没有 RPM 或速度 PID。
- 正式烧录优先使用 Release，并在连接电机前完成分阶段供电和安全门检查。

## 第三方代码

STM32 工程包含 ST HAL/CMSIS 与 FreeRTOS 源码，其许可证保留在对应目录中。本仓库暂未添加覆盖全部内容的根许可证。

