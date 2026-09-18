# Step 6A：HC-SR04 集成与验收清单

## 1. 本步骤边界

本步骤完成从 HC-SR04 到 STM32 状态发布的完整测距数据链：

```text
PB5 Trigger -> HC-SR04 -> PA15 Echo/EXTI -> TIM2 1 MHz
-> HC-SR04驱动状态机 -> SensorTask -> RobotState
-> OLED / UART / W25Q64
```

本步骤不实现自动倒车或转向。自动避障属于上层控制策略；距离无效时，上层 AUTO 控制必须采用禁止前进或停车的失效安全策略。

## 2. 固定软件资源

| 资源 | 配置 |
|---|---|
| TRIG | PB5，推挽输出，空闲低 |
| ECHO | PA15，双边沿 EXTI；外部分压下臂下拉，内部不上下拉 |
| 定时器 | TIM2，1 MHz，1 us/计数，16位自由运行 |
| EXTI IRQ | EXTI15_10，优先级5 |
| TIM2 IRQ | 优先级5，提供30 ms硬件超时 |
| SensorTask | 50 ms任务周期，100 ms测距触发周期 |
| 距离有效位 | `SENSOR_VALID_DISTANCE`，bit1 |
| 聚合错误位 | `ROBOT_ERROR_SENSOR`，bit2 |

ISR只记录Echo边沿和TIM2计数，不调用RTOS、RobotState、OLED或MotorDriver。

## 3. 硬件接线

标准 HC-SR04 按稳定5 V模块处理：

```text
稳定5.0 V  -> HC-SR04 VCC
公共GND    -> HC-SR04 GND
STM32 PB5  -> HC-SR04 TRIG

HC-SR04 ECHO -- 10 kΩ --+-- STM32 PA15
                         |
                        15 kΩ
                         |
                        GND
```

要求：

- Echo禁止直接连接PA15。
- 10 kΩ/15 kΩ建议使用1%电阻；5 V Echo在PA15处约为3.0 V。
- HC-SR04的GND必须与STM32共地。
- 模块VCC附近放100 nF和约10 uF去耦。
- 当前系统4.5 V原始轨不能被当成已经验收的稳定5 V。增加5 V升压，或更换明确支持3.3 V供电/逻辑的超声波模块。
- 改线必须先断电；STM32断电时不要单独给Echo供电。

## 4. 软件状态机

```text
IDLE
  -> 发送至少10 us Trigger
WAIT_RISING
  -> Echo上升沿记录rise counter
WAIT_FALLING
  -> Echo下降沿计算(uint16_t)(fall-rise)
RESULT_READY
  -> SensorTask消费结果后回到IDLE

WAIT_RISING/WAIT_FALLING
  -> 超时或异常脉宽 -> INVALID -> IDLE
```

关键规则：

- 两次Trigger实际间隔至少60 ms，正常约100 ms。
- 等待Echo总超时约30～40 ms，不允许永久BUSY。
- 合法距离范围为20～4000 mm。
- 无Echo、毛刺和越界结果不能写成0 mm。
- 单次测量最多处理8个Echo边沿，超出预算立即判无效并屏蔽本轮Echo中断。
- 任一更新的超时、毛刺或越界结果立即清除距离有效位，不能继续发布旧距离。
- `ROBOT_ERROR_SENSOR`由SensorTask统一按“MPU无效或距离无效”计算。

## 5. 断电检查

- 确认PB5只接TRIG，PA15只接分压中点。
- 确认10 kΩ位于Echo与PA15之间，15 kΩ位于PA15与GND之间。
- 确认传感器、分压下端、STM32为公共地。
- 确认没有把5 V接入STM32的3.3 V轨。
- 确认PA15没有被重新配置为完整JTAG；项目必须保持SWD模式。

## 6. 上电测试顺序

1. 暂不连接PA15，只给超声波模块供电，测量VCC。
2. HC-SR04 Echo和PA15都暂不接：把稳定5 V临时加到10 kΩ上端，静态测分压中点应约3.0 V且不得超过3.3 V；随后断电并移除临时5 V。
3. 将Echo接到10 kΩ上端，断电确认电阻位置后，再把分压中点接PA15并烧录Release固件。
4. 将平整目标放在10 cm、50 cm、100 cm处分别测试。
5. 查看OLED距离，并通过调试器查看RobotState快照和驱动诊断。
6. 连续运行至少10分钟；Trigger/Echo峰值和脉宽需用示波器或逻辑分析仪确认，万用表不能准确捕获短脉冲。
7. 断开Echo，验证距离有效位最迟在200 ms内清除；断电接回后验证自动恢复且不会复活故障前旧值。

## 7. 通过标准

- PB5 Trigger高电平不短于10 us。
- 连续Trigger间隔正常约100 ms，任何情况下不得小于60 ms。
- 10 cm、50 cm、100 cm测量误差不超过约±1 cm或±5%。
- 有效测量时`SENSOR_VALID_DISTANCE`置位，OLED、UART和Flash日志使用同一距离。
- 距离失效时OLED显示`---CM`，UART状态发送`0xFFFF`并清距离有效位。
- MPU正常但超声波失效时，MPU有效位仍保留；聚合传感器错误bit2置位。
- 超声波正常但MPU失效时，距离有效位仍保留；聚合传感器错误bit2置位。
- 没有永久BUSY、中断风暴、SensorTask阻塞或任务栈溢出。

## 8. 尚未通过万用表/实机确认的项目

- 5 V供电是否稳定。
- Echo分压点的真实高电平。
- Trigger脉宽和100 ms周期。
- 实际距离误差与环境温度影响。
- 电机运行噪声下是否产生毛刺或超时。
- SensorTask栈高水位。

在这些项目完成前，只能声明“软件构建通过”，不能声明HC-SR04硬件验收通过。
