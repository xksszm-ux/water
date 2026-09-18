# Step 7：ADC 电池检测验收清单

## 1. 接线

先断电，并保持电机线断开：

```text
4.5 V 输入正极 ---- R1 10 kΩ ----+---- PA0（ADC1_IN0）
                                  |
                                R2 10 kΩ
                                  |
公共 GND -------------------------+
```

- R1、R2 必须都是 10 kΩ；PA0 接在两个电阻的中点。
- 4.5 V 绝对不能直接接 PA0。
- 建议在 PA0 与 GND 之间并联 100 nF 陶瓷电容，靠近 STM32，降低电机噪声。
- 4.5 V 电源负极、AMS 3.3 V 模块 GND、STM32 GND、TB6612 GND 必须共地。

## 2. 万用表到货后的上电顺序

1. 不接 STM32，先测 AMS 输出，确认极性正确且约为 3.3 V。
2. 断电后接 STM32，仍断开 TB6612 的电机输出和四个电机。
3. 上电测 PA0 对公共 GND：应约等于输入电压的一半；4.5 V 输入时约 2.25 V，且必须低于 3.3 V。
4. 烧录 Release 固件，观察 OLED 电池电压和调试器 Watch。
5. 电池检测正常后，才进入 Step 4 的轮子悬空、10% PWM 测试。

## 3. 调试器 Watch 项

在 `RobotState_GetSnapshot()` 处暂停并观察 `robot_status`，或临时调用该函数取得快照：

- `battery_valid`：正常采样后应为 `true`。
- `battery_adc_raw`：当VDDA约3.3 V时，4.5 V输入理论约`2792`。
- `battery_mv`：应接近万用表测得的输入电压。
- `error_status & 0x20`：ADC 故障位；正常后应为 0。
- `error_status & 0x02`：低压位；达到暂定阈值后置 1。
- 电池数据超过 1500 ms 不更新时，`battery_valid=false`，电机必须进入 STBY。
- `g_battery_step7_self_test_passed`：BatteryTask启动后必须为`true`。
- `g_power_supply_fault_latched`：稳定3.3 V供电时应为`false`；一旦变为`true`，必须复位MCU才能清除。
- 在`BatteryTask_Entry()`采样完成处观察`reading`：`vrefint_raw_average`在VDDA约3.3 V时约1489，`vdda_mv`应约3300，`safety_voltage_mv`应略低于显示值。

理论参考值（未做硬件误差校准）：

下表假定VDDA为3.3 V；新算法会同时读取VREFINT，因此VDDA变化时raw会变化，但换算不会继续错误地固定使用3300 mV。

| 输入电压 | PA0 约值 | PA0 raw 约值 |
|---:|---:|---:|
| 4.50 V | 2.25 V | 2792 |
| 4.20 V | 2.10 V | 2606 |
| 3.80 V | 1.90 V | 2358 |

## 4. 当前保护策略

- 采样周期：500 ms。
- 每次读取：动态切换VREFINT和PA0，各丢弃首样后执行8次“DMA长度1”采样。
- 启动或ADC失效后，连续2个安全样本才允许电机。
- 暂定低压触发：`<= 3800 mV` 连续 2 次。
- 暂定严重低压：`<= 3500 mV` 立即触发。
- 暂定恢复：`>= 4000 mV` 连续 3 次。
- ADC 初始化失败、DMA 超时或数据超过 1500 ms 未更新：立即禁止电机。
- 由于BatteryTask周期为500 ms，严重低压从发生到软件采到并停车最坏约510 ms；普通低压两次确认最坏约1010 ms。
- 3.3 V轨跌过PVD Level 7（标称约2.9 V）时，PVD ISR直接停车并永久锁存到复位；它是ADC之外的兜底，不代替电池阈值。

这些阈值只是软件占位值。未确认电池化学体系、AMS 模块实际压差和万用表误差前，不能把低压保护判定为硬件验收通过。

## 5. 禁止事项

- 没有万用表时，不做低压阈值测试，不给电机带载。
- 不用手拔电源模拟低压；这会同时让 MCU 掉压，无法验证保护逻辑。
- 不把 DMA 长度直接改成 8。当前 ADC 是单次转换模式，驱动通过 8 次“长度 1”的 DMA 转换采样。
- 不在CubeMX中新增VREFINT第二Rank；保持单Rank，由`battery_adc.c`动态切换通道。
- 不在 SensorTask 再读 ADC；ADC1/DMA 由 BatteryTask 独占。
- 不为PVD增加软件解锁；供电故障锁存后必须先查清供电再复位。
- PVD当前由应用代码配置。若以后CubeMX也启用PVD中断，必须避免重复定义`PVD_IRQHandler`。
- 不把 Debug 固件作为最终烧录版本；当前 Debug Flash 已使用 83.40%。
