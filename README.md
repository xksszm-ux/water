# STM32–ESP32 FreeRTOS Robot

**未完成原型，尚未通过硬件验收。** 当前交付是默认 STOP-only 的软件集成基线；构建和电脑测试通过不代表开发板、外设或整车正常工作。2026-10-05已按用户要求烧录ESP32 6.1并观察约45秒启动/UART状态，仍有STM32错误位；详细证据和限制见[工程说明](docs/ENGINEERING.md)第10节。

用户最新明确仅检测软件，尚未接入相关外设且PA0未接。此前0x5E及约3.2 V上报不作为软件缺陷或有效电池测量证据；本阶段只运行电脑检查与构建，保留安全保护，后续范围以HANDOFF为准。

## 唯一主工程

后续维护本仓库：`stm32/` + `esp32/` + `shared/` + `software_checks/`。上级目录的 `robot/`、`esp32/` 是保留副本，不同步本轮改动，不应混用其固件或文档。

STM32负责实时输出、控制仲裁、电池保护和传感采集；ESP32负责BLE授权/会话、UART安全握手和状态呈现。BLE DRIVE仍拒绝，STM32仲裁入口也拒绝CAN/AUTO非零运动，避免与ESP32零输出握手冲突；CAN STOP继续全局生效。UART底层运动路径保留用于软件验证与后续开发，尚未通过BLE开放，实际使用仍需硬件验收。

```text
手机/BLE → ESP32授权与STOP请求 → UART V1 → STM32仲裁 → MotorTask → 驱动接口
                                      ↑                  ↓
                        STATUS / DIAGNOSTICS ← RobotState / RTOS诊断
```

## 代码分类与阅读入口

按“启动 → 任务/链路 → 协议与控制 → 驱动”阅读；同一模块的 `.c` 与 `.h` 放在一起。目录分类不改变现有控制行为。

文件显示顺序及CMake源码列表顺序不决定执行顺序；启动由main/app_main的调用链决定，任务由RTOS优先级、阻塞和事件调度。当前27个STM32应用/自编驱动C文件及9个ESP32 C文件均已纳入构建，目录与引用审计结果见[验证记录](software_checks/README.md)。

```text
stm32/
├─ Core/                          CubeMX启动、外设配置、中断、RTOS配置
├─ Application/
│  ├─ Tasks/                      任务创建及7个业务任务、任务健康诊断
│  ├─ Services/
│  │  ├─ Control/                 控制权、序号、STOP及命令仲裁
│  │  ├─ Protocol/                UART帧解析与编解码
│  │  ├─ State/                   共享状态、有效性与过期处理
│  │  ├─ Power/                   电池保护与PVD供电门控
│  │  └─ Storage/                 Flash日志格式与扫描
│  └─ SelfTests/                  原有电机/电池分步自检代码
├─ Drivers/                       按外设分类的驱动，保留原位置
└─ Middlewares/                   FreeRTOS及CMSIS适配层
esp32/main/
├─ main.c                         启动入口
├─ ble/                           BLE授权、会话、状态与生命周期
├─ uart/                          UART收发任务与STOP握手策略
├─ protocol/                      UART/BLE编解码
└─ self_test/                     固件启动时使用的协议自检
shared/                           两端共用诊断与CAN反馈定义
software_checks/
├─ run.py                         电脑检查统一入口
├─ logic/                         仲裁、握手、BLE生命周期、任务健康
├─ protocol/                      协议互通与状态/诊断编码
├─ integration/                   连续双端软件场景、通信调度
├─ drivers/                       UART/CAN生产驱动及Flash日志边界检查
├─ startup/                       两端初始化失败检查及ESP32声明替身
└─ stubs/、startup_stubs/、driver_stubs/  按替换边界区分的STM32/RTOS声明
```

第一次读控制逻辑，可从 [MotorTask单周期](stm32/Application/Tasks/motor_task.c) 看STOP如何清旧命令，再读 [控制仲裁](stm32/Application/Services/Control/control_arbiter.c) 和 [UART命令分发](stm32/Application/Tasks/communication_task.c)。手机侧从 [BLE命令入口](esp32/main/ble/robot_ble.c) 进入 [UART链路](esp32/main/uart/robot_uart_link.c)。厂商库与生成的外设配置保持原布局；SelfTests中的电机分步代码不属于本轮可运行的电脑检查，也不授权执行电机测试。

## 本轮软件交付（2026-09-22）

- 修复tick零传感样本不失效、迟到字节复活过期半帧两项电脑端复现缺陷。
- 状态导出时检查数据过期；修正日志消息时间零哨兵；保留新命令恢复规则。
- ESP32安全握手提取为固件和测试共用的纯逻辑；保守处理同tick状态，避免长期在线时与初始ACK比较溢出。
- 单周期电机逻辑与任务调度分开，测试直接执行生产控制逻辑。
- STM32约1 Hz发送0x82诊断帧，ESP32接收并在现有串口日志报告。BLE状态格式不变。
- MPU/Flash显式稳定等待改为任务延时；新增编译器静态栈报告，不声称已测得栈余量。
- 一个入口运行主机检查及两端构建，不访问硬件。
- 2026-09-23新增连续双端软件场景：STOP握手、丢ACK/队列满重试、错误CRC、内部运动命令与安全恢复、状态过期、计时回绕。
- 2026-10-01复现并消除CAN/AUTO先运动再被ESP32停止的语义冲突：默认拒绝CAN非零运动，保持零输出在线；16组检查与两端构建重新通过，详见验证记录。
- 2026-10-02补齐CAN最新决策、序号及拒绝/重放统计的UART 0x83反馈和加密BLE只读入口；区分过期/离线与尚无CAN事件，原状态包不变。电脑反馈链及两端构建通过，真实读取待验。
- 2026-10-03逐项检查RTOS/通信/传感显示存储/BLE/PVD软件路径，修复采样时间刷新、UART状态读取、CAN ISR预算及状态过期导出；新增4组生产逻辑边界检查，详细结果见工程说明第9节。
- 2026-10-04按职责分类移动45个文件，同步构建/检查/文档路径；32个移动的固件源文件和头文件内容未变，20组检查及双端构建重新通过。目录导航见上文。
- 2026-10-05将ESP32构建基线同步到正式ESP-IDF 6.1；CMake要求6.1.x环境，重新生成配置、固定100 Hz tick并清除无效的GATT client显式配置。20组检查、双端构建及旧SDK拒绝检查通过，协议与STOP-only不变；SDK警告和验证限制见验证记录。
- 2026-10-07新增5组生产驱动/BLE事件检查，修复MPU6050失败初始化残留ready，补GitHub CI和PATH工具选择；最终验证与提交状态见HANDOFF及验证记录。仍不进行硬件操作。

## 阅读顺序

继续开发先读 [统一交接与下一步](HANDOFF.md)。

1. [整体架构、任务表、技术决策、稳定性与真实缺陷](docs/ENGINEERING.md)
2. [本轮检查结果、工具要求及复现命令](software_checks/README.md)
3. [三分钟介绍和面试追问提纲](docs/INTERVIEW.md)
4. [UART基础协议](stm32/docs/STEP9_UART_PROTOCOL.md)、[BLE协议](esp32/docs/STEP10_BLE_PROTOCOL.md)

## 软件验证

在已激活ESP-IDF 6.1的PowerShell中，将CMake、ARM GCC、Ninja及LLVM加入PATH，从本仓库根目录执行（本机完整命令见验证记录）：

```powershell
./verify.ps1
```

允许 `-Python`、`-CMake` 指定可执行文件；主机C编译器/链接器通过 `ROBOT_HOST_CLANG`、`ROBOT_HOST_LINK` 指定。本机完整命令见验证文档。

[GitHub Actions](.github/workflows/software.yml)在push/PR时运行Windows主机检查、STM32 Debug/Release和ESP-IDF 6.1构建；不连接硬件、不部署。CI结果以实际运行记录为准，不把新增工作流视作已通过。

最新实际验证（2026-10-07）：主工程已上传main；[CI运行37584729464](https://github.com/xksszm-ux/water/actions/runs/37584729464)的25组生产逻辑检查、STM32 Debug/Release、ESP-IDF6.1构建全部通过。最后GATT用例的执行证据已补齐；本机Windows DLL拦截仍独立保留，硬件/真实RTOS未验证。工具链、产物尺寸与警告见[验证记录](software_checks/README.md)。

本轮统一入口结果（2026-10-05）：20组软件检查通过；STM32 Debug/Release、ESP-IDF 6.1构建通过，5.4.4被版本约束拒绝。6.1有5项SDK内部CMake依赖警告，未修改SDK消除它们。任务健康策略把不同任务的超期故障锁存并通过0x82修订2诊断帧输出，但期限尚未通过真实调度校准。生产UART/CAN回调、日志层及ESP32 UART启动已有替身检查。随后仅ESP32产物完成实际烧录/有限启动观察，STM32本次产物未烧录；电脑检查仍不执行真实RTOS/硬件，设备观察也未覆盖手机授权、故障恢复、DMA时序或外围器件验收。

## 仍未完成

BLE运动端到端转发、自主避障、硬件看门狗启用、编码器速度闭环、OTA未完成。已有BLE授权、驱动、任务并发与安全停车代码的真实效果待验收。硬件验收、供电校准、任务栈水位和长稳测试全部单列待办，不能引用历史文字宣称通过。

第三方ST HAL/CMSIS与FreeRTOS许可证保留在对应目录。本仓库未添加覆盖全部内容的根许可证。
