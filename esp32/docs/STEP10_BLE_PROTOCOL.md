# Step 10A BLE安全联调协议

本阶段目标是先验证手机到ESP32、ESP32到STM32的安全停车链路。非零运动被编译期设计明确禁用；通过本阶段不等于BLE运动控制完成。

## GATT

- 设备名：`SmartRobot-V1`
- Service：`7e57a000-bbcd-4b20-9f0d-3c8fa62e1000`
- Command：`7e57a000-bbcd-4b20-9f0d-3c8fa62e1001`，加密 Write Request
- Status：`7e57a000-bbcd-4b20-9f0d-3c8fa62e1002`，加密 Read + Notify
- 单连接，LE Secure Connections Just Works，加密并保存一个绑定；显式分发LTK和Identity Key以支持RPA身份解析。

Just Works不能抵抗主动中间人攻击，因此本阶段只能称为“加密 + 物理授权换绑”，不能称为已认证控制。非零运动仍被禁用；开放运动前还必须增加受认证的配对方案和已绑定手机白名单，不能使用源码硬编码的通用密码。

## 绑定恢复

- 应用已经正常启动后，运行时长按开发板`BOOT/GPIO0`约3秒，会先请求UART STOP，再在NimBLE Host上下文断开连接并删除唯一旧手机绑定，随后开放60秒换绑窗口。
- 第一次绑定也必须先执行该物理动作；窗口外只允许NVS中已经绑定的同一手机，陌生手机连接后会被立即STOP并断开。
- 必须松开BOOT后才能再次触发；不要按住BOOT同时复位或上电，否则ESP32会进入下载模式。
- 不会因为远端重复配对请求而自动删除旧绑定，也不会自动擦除整个NVS。
- 未完成加密的连接10秒超时并被主动断开，避免占住唯一连接。

## Command，固定10字节，小端

| Offset | 字段 |
|---:|---|
| 0 | version=`1` |
| 1 | opcode：`0=STOP`，`1=DRIVE预留` |
| 2..3 | phone sequence |
| 4..5 | left PWM，`-1000..1000` |
| 6..7 | right PWM，`-1000..1000` |
| 8..9 | CRC16/CCITT-FALSE，覆盖0..7 |

Step 10A只接受`STOP + left=0 + right=0`。所有DRIVE都返回ATT拒绝并再次请求UART安全STOP。

Command的ATT写成功只表示“ESP32已受理STOP请求”，不等于STM32已经停车。最终完成条件必须同时满足：UART online、STATUS fresh、左右实际PWM均为0。

## Status，固定20字节，小端

| Offset | 字段 |
|---:|---|
| 0 | version=`1` |
| 1 | type=`0x81` |
| 2..3 | status sequence |
| 4 | link flags：bit0 UART online，bit1 BLE connected，bit2 STATUS fresh，bit3 encrypted |
| 5 | STM32 valid flags |
| 6..7 | battery mV，无效=`0xFFFF` |
| 8..9 | applied left PWM |
| 10..11 | applied right PWM |
| 12..13 | distance mm，无效=`0xFFFF` |
| 14 | mode |
| 15 | STM32 error status |
| 16 | owner |
| 17 | reserved=`0` |
| 18..19 | CRC16/CCITT-FALSE，覆盖0..17 |

通知周期200ms，默认ATT MTU 23刚好可承载20字节值。

当`STATUS fresh`为0时，battery/distance为`0xFFFF`、valid flags为0、左右PWM为0、mode/error为`0xFF`；手机不得把这些占位字段当作真实机器人状态。

已知STOP向量（sequence=`0x1234`）：

`01 00 34 12 00 00 00 00 19 1F`

## 手机验收顺序

1. 电机保持断开。应用正常启动后长按BOOT约3秒，松开后在60秒内搜索并连接`SmartRobot-V1`；不要按住BOOT复位。
2. 完成系统配对/加密。
3. 订阅Status，确认每约200ms收到20字节数据。
4. 向Command写入合法STOP帧，确认ATT写成功，串口日志出现`BLE STOP accepted`；继续等到状态满足UART online + fresh + PWM=0。
5. 写入DRIVE帧，必须被拒绝；STM32实际PWM继续为0。
6. 断开BLE，ESP32必须请求STOP，UART链路重新完成STOP ACK与零PWM STATUS握手。

## 后续开放运动前必须完成

- 把UART链路重构成`OFFLINE/STOPPING/SAFE_READY/MOTION_ACTIVE`状态机；现有ONLINE只允许零PWM。
- BLE会话token、旧连接命令隔离、250ms手机命令deadman。
- UART MOTOR每不超过100ms发送递增序号，STM32 300ms最终超时。
- ESP32重启后保留至少550ms控制帧静默，避免与STM32旧序号会话冲突。
- 低电、UART失联、BLE断连、取消订阅、加密丢失全部停车；恢复后不得自动重放旧目标。
- 把Just Works替换为具备MITM保护和物理授权的正式配对方案，并只允许已授权的绑定身份进入运动态。
- 电源、TB6612和四电机硬件验收通过后，才能单独授权非零PWM测试。
