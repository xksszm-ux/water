# Step 9：STM32–ESP32 UART V1 协议与验收

## 1. 当前结论

STM32 端软件已完成并通过 Debug/Release 构建。尚未与 ESP32 或 USB-TTL 做实物联调，因此当前结论是“软件完成、硬件链路待验收”，不能写成整机通信已经通过。

V1 使用二进制协议，不使用 JSON。状态中的左右数值是实际施加的 PWM 千分比 `-1000..1000`，不是 RPM。

## 2. UART 配置和接线

- USART1：115200 bit/s、8 数据位、无校验、1 停止位、无流控。
- STM32 `PA9/USART1_TX` → ESP32 UART RX。
- STM32 `PA10/USART1_RX` ← ESP32 UART TX。
- STM32 GND 与 ESP32 GND 必须共地。
- 两侧都是 3.3 V 逻辑，不要把 4.5 V 接到 PA9、PA10 或 ESP32 GPIO。
- PA10 已配置内部上拉，ESP32 未连接时不会悬空产生大量伪帧。

具体 ESP32 UART GPIO 在 Step 10 的 ESP32 工程中冻结；不要只按教程默认引脚盲接。

## 3. 帧格式

所有多字节字段均为小端序。帧总长度为 `10 + payload_length`，最大 42 字节。

| 偏移 | 长度 | 字段 | 说明 |
|---:|---:|---|---|
| 0 | 1 | SOF0 | `0xA5` |
| 1 | 1 | SOF1 | `0x5A` |
| 2 | 1 | Version | V1=`0x01` |
| 3 | 1 | Type | 消息类型 |
| 4 | 1 | Flags | bit0=`ACK_REQ`，其余必须为 0 |
| 5 | 2 | Sequence | UART 控制序号，`uint16_t` 小端 |
| 7 | 1 | Payload length | `0..32` |
| 8 | N | Payload | 类型相关载荷 |
| 8+N | 2 | CRC16 | 低字节在前 |

CRC 使用 CRC-16/CCITT-FALSE：`poly=0x1021`、`init=0xFFFF`、不反射、`xorout=0`。CRC 覆盖 `Version` 到 `Payload`，不包含两个 SOF 和 CRC 自身。标准向量 `123456789` 的结果为 `0x29B1`。

## 4. 消息定义

### ESP32 → STM32

| Type | 名称 | Payload |
|---:|---|---|
| `0x01` | MOTOR_CONTROL | `left i16`、`right i16`、`control_flags u8`；flags bit0 必须为 1 |
| `0x02` | STOP | 长度 0 |
| `0x03` | SET_MODE | 长度 1：BLE=`0`，AUTO=`1` |
| `0x04` | STATUS_REQUEST | 长度 0 |
| `0x05` | HEARTBEAT | 长度 0 |

MOTOR_CONTROL 的左右值必须在 `-1000..1000`。越界值直接拒绝，绝不能钳位成满 PWM。`left=0,right=0` 会按安全 STOP 处理；常规停车优先使用独立的 `0x02`。

### STM32 → ESP32

`0x80 ACK` 的 4 字节 Payload：

| Byte | 内容 |
|---:|---|
| 0 | 被确认的请求 Type |
| 1 | Result |
| 2 | 当前模式 |
| 3 | 当前控制 owner |

`0x81 STATUS` 的 12 字节 Payload：

| Byte | 内容 |
|---:|---|
| 0–1 | 电池 mV；无效=`0xFFFF` |
| 2–3 | 左侧实际 PWM 千分比，`int16_t` |
| 4–5 | 右侧实际 PWM 千分比，`int16_t` |
| 6–7 | 距离 mm；无效=`0xFFFF` |
| 8 | 模式：BLE=`0`，AUTO=`1` |
| 9 | Robot error bitmask |
| 10 | valid flags：bit0=电池，bit1=距离 |
| 11 | owner：CAN=`0`，UART=`1`，TEST=`2`，NONE=`0xFF` |

ACK Result：

| 值 | 名称 | 含义 |
|---:|---|---|
| 0 | OK | 已接受；是否实际转动仍以 STATUS 为准 |
| 1 | DUPLICATE | 重复或旧序号，未刷新运动超时 |
| 2 | BAD_LENGTH | 长度错误 |
| 3 | BAD_FLAGS | 标志错误 |
| 4 | RANGE_ERROR | 数值越界 |
| 5 | VERSION_ERROR | 版本不支持 |
| 6 | UNSUPPORTED | Type 不支持 |
| 7 | MODE_DENIED | 当前模式不允许该控制源运动 |
| 8 | OWNER_BUSY | 另一个控制源仍持有租约 |
| 9 | SAFETY_BLOCKED | 电池/ADC 安全条件不允许运动 |
| 10 | QUEUE_FULL | 响应资源不足，V1 保留 |
| 11 | HANDOFF_STOP | 所有权交接或停车保护期，只停车不运动 |
| 12 | INTERNAL_ERROR | 内部提交失败 |

## 5. 控制、序号和超时规则

- UART 非零运动只允许在 BLE 模式；CAN 非零运动只允许在 AUTO 模式。
- 模式只能通过 UART `SET_MODE` 修改。切换模式会先停车、清 owner，并进入至少 20 ms 停车保护。
- CAN、UART 各自维护独立序号。UART 前向窗口为差值 `1..32767`；CAN 为 `1..127`，均支持自然回绕。
- 语义合法但重复/旧的运动帧不会执行，也不会刷新 MotorTask 的 300 ms 命令超时。
- 有效控制流静默超过 500 ms 才允许建立新序号会话。持续重放旧帧不能重新启动机器人。
- 第一条合法非零运动取得 owner；只有 owner 的新运动帧刷新 300 ms 租约。
- owner 超时后，另一个来源的第一条新运动帧只触发 `HANDOFF_STOP`；下一条递增序号且 20 ms 保护结束后才能运动。
- 任意来源、任意旧序号的合法 STOP 都会停车。STOP 不取得 owner、不刷新租约，也不会倒退序号基线。
- 低压或 ADC 无效时，前向序号会被消费，但不会取得 owner、不会入队；恢复后必须发送更大的新序号。
- ESP32 控制运动时应每 100 ms 以内发送一条递增序号的新 MOTOR_CONTROL。HEARTBEAT 只能维持 UART 链路状态，不能维持电机运动。
- ESP32 必须采用 stop-and-wait：最多保留 1 个等待 ACK 的状态修改请求。ACK 丢失时可重发同一序号，但重复帧不延长运动超时。

## 6. DMA 和任务约束

- RX 使用 DMA1 Channel 5 循环模式和 UART IDLE 事件；`RxEventCallback.Size` 是 DMA 当前写位置，不是本次新增长度。
- DMA 只启动一次。IDLE 不是帧边界，不能在每次 IDLE 后重新启动 DMA。
- ISR 只搬运字节、锁存错误和通知 CommunicationTask；CRC、协议、RobotState 和电机仲裁全部在任务上下文执行。
- 当前没有 USART1 TX DMA，发送使用 `HAL_UART_Transmit_IT()`；不要调用 `HAL_UART_Transmit_DMA()`。
- RX 软件环溢出会保留最新数据并触发源故障；如果 UART 是 owner，会立即停车。
- UART 错误恢复使用全双向 `HAL_UART_Abort()`，随后 DeInit/Init 并重新启动 RX DMA。

## 7. 已验证的软件项目

- Debug 和 Release 均成功生成 ELF、HEX、BIN。
- 本次改动文件使用 ARM GCC `-Wall -Wextra -Werror` 检查通过。
- 正式配置：`MOTOR_STEP4_TEST_ENABLED=0`、`CAN_DRIVER_INTERNAL_LOOPBACK_TEST=0`。
- 两个测试宏为 1 的代码分支也完成严格语法编译。
- CRC 测试通过：

```text
123456789 -> 0x29B1
STOP       -> 0x4EB7
HEARTBEAT  -> 0x68E7
MOTOR      -> 0x4062
STOP_ACK   -> 0xFDE0
```

测试帧，CRC 低字节在前：

```text
STOP, seq=1, ACK_REQ:
A5 5A 01 02 01 01 00 00 B7 4E

HEARTBEAT, seq=0:
A5 5A 01 05 00 00 00 00 E7 68

MOTOR, seq=0x1234, left=500, right=-500, ACK_REQ:
A5 5A 01 01 01 34 12 05 F4 01 0C FE 01 62 40

STOP ACK（mode=BLE, owner=UART）:
A5 5A 01 80 00 01 00 04 02 00 00 01 E0 FD
```

## 8. 待实物验收

1. 先保持电机断开，只接 PA9、PA10 和公共 GND。
2. 用 ESP32 Step 10 固件或 3.3 V USB-TTL 发送 HEARTBEAT、STATUS_REQUEST 和 STOP。
3. 检查周期 STATUS、ACK、坏 CRC 后重同步、半帧超时和粘包。
4. 观察 `g_uart_*` 计数、UART driver diagnostics、任务心跳与栈高水位。
5. 万用表完成供电验收后，轮子悬空，再测试递增序号运动、300 ms 超时、低压拒绝和 CAN/UART 交接。

