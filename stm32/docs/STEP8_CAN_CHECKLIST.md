# Step 8：CAN软件与硬件验收清单

## 1. 已确认的配置

- CAN1：PA11=CAN_RX，PA12=CAN_TX。
- APB1=36 MHz，Prescaler=4，BS1=15TQ，BS2=2TQ。
- 波特率：`36 MHz / 4 / (1+15+2) = 500 kbit/s`。
- 采样点：`16/18 = 88.9%`。
- 自动重发和Automatic Bus-Off Management均已开启。
- TX、RX0和CAN1_SCE三路中断优先级均为5，可调用FreeRTOS ISR API。

注意：`.ioc`必须保存`CAN.NART=DISABLE`，生成代码必须是
`AutoRetransmission=ENABLE`。NART表示“No Automatic Retransmission”，不要把
它误设为ENABLE。

## 2. 固定协议

统一使用标准11位ID、数据帧、小端序、DLC=8。

### 0x101：机器人状态，STM32每100 ms发送

| 字节 | 内容 |
|---|---|
| 0～1 | 电池电压mV；无效时`0xFFFF` |
| 2～3 | 左侧实际PWM，`int16_t`，-1000～1000 |
| 4～5 | 右侧实际PWM，`int16_t`，-1000～1000 |
| 6 | 模式：0=BLE，1=AUTO |
| 7 | RobotError位图；bit6为CAN错误 |

Byte2～5不是RPM，也不是闭环速度。

### 0x100：运动控制，STM32接收

| 字节 | 内容 |
|---|---|
| 0～1 | 左PWM，`int16_t`，-1000～1000 |
| 2～3 | 右PWM，`int16_t`，-1000～1000 |
| 4 | 模式：非零运动只允许1=AUTO |
| 5 | flags：bit0=enable，其余必须为0 |
| 6 | 滚动序号 |
| 7 | 协议版本，固定1 |

- 发送周期应为50～100 ms；MotorTask在300 ms没有新命令后停车。
- 越界PWM、错误版本、非法模式、保留位或DLC错误会被直接拒绝，不能钳位成满PWM。
- 序号采用8位前向窗口：差值1～127为新帧，0为重复，128～255为旧帧；
  连续300 ms没有合法控制帧后才允许任意序号建立新会话。
- 重复/旧运动帧不会刷新MotorTask的300 ms命令超时。
- STOP（enable=0或左右均为0）不受旧序号阻止，并保持当前工作模式。
- 合法STOP有独立接收槽和任务级锁存。MotorTask会丢弃其后已经排队的运动帧，
  至少执行一个10 ms停车周期；控制端必须继续发送递增序号的新运动帧才能恢复。

### 0x102/0x103：PID预留

`0x102`可解析目标、Kp/Ki/Kd/输出限制、数值、序号和版本。V1没有编码器和速度PID，所以合法配置也会通过`0x103`返回结果4=`UNSUPPORTED`；不能假装参数已经生效。

`0x103`固定布局：Byte0=2（PID配置响应），Byte1=请求序号，Byte2=结果，
Byte3=目标，Byte4=参数，Byte5～6=0，Byte7=协议版本1。响应使用4槽FIFO，
只有收到TX完成中断后才出队；超时或恢复不会静默丢失响应。

## 3. 现在即可完成的测试

### 构建

正式固件保持：

```c
#define CAN_DRIVER_INTERNAL_LOOPBACK_TEST 0U
```

Debug使用`-Og -g3`，Release使用`-Os -g0`。两者都必须成功生成
ELF/HEX/BIN且无警告；正式烧录仍优先使用Release。

当前结果：

```text
Debug:   FLASH 44612 B / 64 KB (68.07%), RAM 15712 B / 20 KB (76.72%)
Release: FLASH 38552 B / 64 KB (58.83%), RAM 15696 B / 20 KB (76.64%)
```

### 协议向量

输入：电池4200 mV、左PWM=+350、右PWM=-200、AUTO、错误`0x22`。

状态帧期望：

```text
ID 0x101: 68 10 5E 01 38 FF 01 22
```

控制帧期望：

```text
ID 0x100: 5E 01 38 FF 01 01 2A 01
```

还要确认DLC=7、PWM=1001、mode=2、版本=2和保留flags非0均被拒绝。

### STM32内部Loopback（不需要收发器和万用表）

1. 保持电机断开。
2. 将`Drivers/CAN/can_driver.h`中的`CAN_DRIVER_INTERNAL_LOOPBACK_TEST`临时改为1。
3. 构建并烧录Release。
4. 调试器Watch观察`g_can_loopback_self_test_passed`，应从0变成1。
5. `g_can_protocol_reject_count`应保持0。
6. 自测先发送不在RX过滤器内的`0x101`验证独立TX中断，再发送安全STOP
   `0x100`验证RX、解码和MotorTask提交；要求2次TX完成且0次超时。
7. 测试结束必须把宏改回0并重新构建；Loopback固件不能用于外部CAN总线。

本次已经编译检查过宏为0和宏为1的两条代码分支，但尚未代替用户烧录实机。

## 4. SN65HVD230后续接线

```text
STM32 PA12 CAN_TX  -> SN65HVD230 D/TXD
STM32 PA11 CAN_RX  <- SN65HVD230 R/RXD
STM32 3.3 V        -> VCC
公共GND            -> GND
RS                 -> GND（高速模式）
CANH               -> 总线CANH
CANL               -> 总线CANL
```

- SN65HVD230只能使用3.3 V，不能接4.5 V或5 V。
- VCC旁加100 nF去耦。
- CANH/CANL使用双绞线。
- 总线只有两个物理末端，各放一个120 Ω；模块自带终端时不能重复添加。
- 节点之间仍需连接公共地。

## 5. 容易误判的现象

- 正常模式下只有一个CAN节点时，没有第二个节点ACK；发送超时、CAN错误和Bus-Off是预期现象，不代表协议代码错误。
- 驱动只允许一个发送帧在途，50 ms未完成会主动Abort，避免三个邮箱永久被自动重发占满。
- CAN中断只收帧、写静态缓冲并通知CanTask，不直接操作RobotState或MotorDriver；
  合法STOP使用独立槽，普通控制使用最新值槽，PID配置使用FIFO。
- CanTask每轮最多处理4帧，防止接收洪泛饿死发送超时和状态上报；到期的
  `0x101`状态帧优先于PID响应发送。
- PA11/PA12同时是USB D-/D+；当前工程没有启用USB。调试CAN时使用ST-Link，不要同时启用USB数据功能。
- Step 9加入UART控制前必须完成控制源仲裁：STOP可由任何来源提交，非零命令只能来自当前控制源。
