# 嵌入式工程检查：2026-10-07

审计基线：`163dafacfc05e2f2bd0e0329c63ec0c597951540`。审计开始时 `git status --short` 为空；后续修复开始时已有3个本审计未跟踪文件并予以保留。只检查本主工程，读取上级AGENTS、项目Ponytail（full）、README、HANDOFF、ENGINEERING、检查README、两端协议及对应生产调用者；不使用外层历史副本。

审计阶段没有修改生产代码、SDK、安全策略或运动开关，没有安装依赖、烧录、连接设备或测试电机。当时新增复现被阻止，以下发现/行号和执行结果保留为163dafa审计快照。随后用户授权依次修复F1/F2，最新结果见下一节，不能把历史阻塞或未修状态当作当前结论。项目没有通过硬件/整车验收。

## 后续修复状态（2026-10-07）

用户明确要求依次解决F1与F2；已完成软件修改，本地尚未提交/推送：

1. F1：修改前旧clock_repro实际运行，确认0x02假Motor故障和新传感样本误清。修复AppTasks健康和RobotState传感/电池过期、运动电源资格入口，在各自短临界区内采tick，移除外部now参数并更新全部调用者；TaskHealth纯逻辑仍由AppTasks统一提供时间/心跳快照，不改期限或保护。修复后AppTasksInitChecks_Run和ClockRepro_Run单独实际返回0/PASS，覆盖保护前抢占、回绕、真实超期/锁存及新样本边界。
2. F2：STEP10_BLE_PROTOCOL.md现与robot_ble.c既有30000ms一致，说明30秒到期发起STOP/断连、约200ms周期和GAP异步确认；ESP32生产代码/策略未改。

当前26组均严格编译完成；统一入口加载checks.dll时4551、未进入全套断言。独立clock_app_tasks.dll也被阻止，随后执行统一入口已生成的app_tasks_init.dll和clock_repro.dll通过，系统保护未改，不能声称策略解除或26组全通过。STM32 Debug/Release重新编译链接、ESP-IDF6.1增量构建成功。可复现命令、工具/尺寸及限制统一见software_checks/README.md和ENGINEERING B13/B14。

clock_repro.py及clock_repro_checks.c已由旧缺陷复现转为修复后回归，当前退出0表示回归通过。审计R1～R4尚未修改；真实抢占/ISR、栈、硬件停车仍待验。下面各节是修复前审计记录，不覆盖本节最新状态。

## 结论与优先级

新确认问题共两个：P2共享时间快照不一致（合并健康和传感两个场景）；P3加密期限文档冲突。没有确认新的P0/P1缺陷。已有的关键任务停摆保护缺口列为P1待验证风险；Storage栈、ESP32发送期限和I2C恢复列为P2待验证风险，不伪装成实机故障。

| 编号 | 等级 / 证据 | 核心影响 | 本轮状态 |
|---|---|---|---|
| F1 | P2 / 静态确认 | 旧观察时间减去新共享记录，下溢后误清传感有效位、永久锁存假健康故障 | 已追踪可达调用交错；复现严格编译完成，运行被4551拦截 |
| F2 | P3 / 静态确认 | 文档承诺10秒加密期限，代码与检查实际为30秒 | 未修；不是据此判定30秒方案不安全 |
| R1 | P1 / 真实RTOS、硬件待验证；已知完成度缺口 | MotorTask不再推进时，任务内300ms停车与STOP消费不能执行 | 未启IWDG，健康只诊断；本轮未注入任务停摆 |
| R2 | P2 / 真实RTOS、硬件待验证 | Storage深层SPI调用被抢占，可能覆盖栈溢出哨兵并全局停机 | 当前Release .su和ELF已核对，无实际高水位/溢出证据 |
| R3 | P2 / SDK故障注入、硬件待验证 | ESP32 UART写入可无限等待，后面的20ms完成期限不能覆盖它 | 核对6.1 SDK及官方说明，未实际制造TX停摆 |
| R4 | P2 / 硬件待验证 | 特定I2C BUSY锁死状态不能由设备重试解除 | 官方errata与现有恢复路径已核对，未确认板上发生 |

## 已确认缺陷

### F1 — P2：共享时间快照不一致，误判新数据过期及健康故障

**证据类型：静态确认；未完成电脑运行复现。** 当前源码提供两个实际可达的交错，合并报告同一根因。

健康调用链与位置：

- `stm32/Application/Tasks/communication_task.c:344`的QueueDiagnostics → `app_tasks.c:160`的AppTasks_GetDiagnostics → `app_tasks.c:178–187`的AppTasks_HealthPoll → `task_health.c:26–35`的TaskHealth_Poll。
- `stm32/Application/Tasks/motor_task.c:40–44`也调用AppTasks_HealthPoll。Motor优先级AboveNormal、Comm为Normal（`app_tasks.c:46,49`）。

触发：启动宽限结束后，Comm先采集时间5000，再被Motor抢占。Motor推进心跳并在5001轮询，把Motor的last_progress_ms更新为5001。Comm恢复后进入临界区，看到相同新心跳，却仍使用5000；`uint32_t(5000-5001)`成为UINT32_MAX，超过100ms期限，锁存Motor故障。后续心跳正常也不会清掉锁存掩码，经0x82送到ESP32。

传感调用链与位置：

- `stm32/Application/Tasks/display_task.c:104`采now → `:107–109`等待I2C锁并初始化OLED → `:115`传旧now给`stm32/Application/Services/State/robot_state.c:36–45`。
- Sensor优先级更高，可在Display等待期间通过`stm32/Application/Tasks/sensor_task.c:89–99,123–138`完成采样、释放锁并发布新timestamp。Storage `storage_task.c:33–34`、Comm `communication_task.c:318–320`、CAN `can_task.c:236–237`也使用锁外采集的观察时间，不能只修Display一个调用者。

若Display保存5000，Sensor随后发布5001，旧now使年龄下溢，清掉刚发布的距离/IMU有效位并置SENSOR错误。共享RobotState受影响，UART/CAN/OLED/日志可能导出无效值；下一次传感发布可以恢复有效位。这属于误失效，不是已证实的机械停车故障。健康故障当前只诊断，不直接停机或复位。

**根因和现有防护不足：** 临界区保护了共享结构，但时间在保护外采集，可能比临界区内看到的记录更旧。正常32位回绕下无符号减法正确，并不代表乱序观察也正确。3秒宽限、有效位、锁存和现有回绕检查没有覆盖此交错。

**最小建议：** 后续在共享状态保护边界内取得判断时间，使其与所读心跳/采样状态一致；对所有时效判断调用者统一核对。只在Display调用前重新取tick仍保留抢占窗口。若保留显式时间接口，应明确有界乱序规则并同时验证真实计数回绕，不能无条件把大时间差当作新鲜。本轮不实施。

**验证：** 新增`software_checks/logic/clock_repro_checks.c`直接链接生产task_health.c、robot_state.c，以顺序调用表示上述交错，只替换RTOS时间/临界区边界。检查当前错误掩码0x02及新传感样本被清，并检查健康故障仍锁存。`clock_repro.py`复用run.py的工具查找、CRT和加载提示。编译-Wall/-Wextra/-Werror完成，加载clock_repro.dll被WinError4551拒绝，没有执行断言。需要可信运行环境执行；真实抢占发生率还需RTOS验证。该程序退出0表示旧缺陷被复现，**不是修复通过**；修复后需改变预期，保留正常回绕用例。

### F2 — P3：BLE加密超时文档10秒，代码和检查为30秒

**证据类型：静态确认。** `esp32/docs/STEP10_BLE_PROTOCOL.md:24`写10秒；`esp32/main/ble/robot_ble.c:30`为30000U，`:1023`设deadline，`:484–511`到期请求STOP并断连。`software_checks/startup/esp_ble_checks.c:170–172`按29999/30000ms验证。

触发是允许配对/旧绑定连接后，迟迟未完成加密。按文档等待10秒会误认为断连机制失效，而实现计划在30秒后发起断连，另受服务周期和GAP回调影响。

根因是文档与实现/检查未同步。现有检查按30秒预期不能发现10秒描述冲突；这不是已发现的授权绕过，也不说明30秒设计本身错误。

最小建议：先确认 intended deadline，统一该文档或常量与检查；如果维持当前30秒，修改一处文档即可，无需重构。此次仅静态核对，没有实机断连证据，也未重新执行对应C用例。

## 待验证风险

### R1 — P1：MotorTask停摆时，现有任务内超时/STOP不提供独立停机

**证据类型：真实RTOS、硬件待验证；这是已记录的完成度限制，不是新复现bug。**

位置/链路：内部UART非零命令 → `control_arbiter.c:183`的Submit → Motor队列 → `motor_task.c:39–173` → `stm32/Drivers/Motor/motor.c:128–182`设置TIM3比较值/STBY。超时判断在`motor_task.c:69–70`，STOP位消费在`:45`；`app_tasks.c:178–188`和`task_health.c:35`只锁存掩码，无独立输出关闭。IWDG没有生产启用路径。

触发前提：电池/供电合格、内部UART确已施加非零输出，随后MotorTask被挂起、阻塞或失去调度，而没有PVD/异常钩子触发。仅执行任务内300ms判断和消费STOP的代码不再运行；Comm即使登记STOP也不能强制该任务消费。TIM3 PWM由外设计数器生成，不能因为任务停止自动推定其关闭；这是结合代码和[ST AN4776定时器说明](https://www.st.com/resource/en/application_note/an4776-generalpurpose-timer-cookbook-for-stm32-microcontrollers-stmicroelectronics.pdf)的风险推断，没有观察板上输出。

已有防护：BLE/CAN非零拒绝减小入口，但UART内部路径仍存在；PVD只针对供电条件；Fault/assert/栈溢出钩子只在相应异常发生后执行。健康诊断不等于看门狗，不等于停机。ESP32状态过期只让无线链路离线和重发STOP，无法替代STM32最终关闭。

最小建议：开放任何实机运动前，落实ENGINEERING既有关键心跳监管/独立停机或IWDG方案；故障复位后安全启动、重新资格和新命令必须一起定义，不能只添加无条件喂狗。

本轮只追踪生产控制/停止入口，没有挂起任务、写非零寄存器或操作电机。未来先断开执行器，在授权测试固件中注入关键任务停摆，观察独立关闭STBY/PWM或有界安全复位；时间上限须先定义并包含看门狗时钟误差。当前不能声明此标准满足，更不能沿用“300ms”作为Motor停摆的保证。

### R2 — P2：Storage 512B栈在SPI深层被抢占时可能触发全局停机

**证据类型：真实RTOS、硬件待验证；有当前构建静态依据。**

配置`stm32/Application/Tasks/app_tasks.c:36`为512B；`stm32/Core/Inc/FreeRTOSConfig.h:152`开启模式2；`app_tasks.c:191–197`的溢出钩子紧急关闭输出后永久循环。不是自动复位。

首次/扇区边界写日志：`storage_task.c:40` → `Services/Storage/storage_log.c:157` Append → `:102` PrepareSector → `:80` InspectSector → `Drivers/W25Q64/w25q64.c:120` Read → HAL_SPI_Receive `:961` → HAL_SPI_TransmitReceive `:1177` → SPI_EndRxTxTransaction `:3672` → SPI_WaitFlagStateUntilTimeout `:3568`（最后四处位于`stm32/Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_spi.c`）。

本轮ARM GCC14.3.1 Release全量重建的.su和ELF中真实嵌套调用为104+80+16+40+56+40+40+24+40=440B，已核对BL而非尾调用，SPI2的Master/2Lines走对应分支。不是把无关函数峰值随意相加。

[Arm Cortex-M3 TRM §5.5.1](https://documentation-service.arm.com/static/6036810d5319e554d4ba108e)定义异常硬件压栈8word/32B；项目`Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c:401`另外保存r4–r11/32B。共约504B，不含可能的额外对齐开销。当前storage_stack=0x20001fec，`Source/tasks.c:864–865`初始栈顶对齐后可用508B，此估算只剩4B；模式2检查底16B（`Source/include/stack_macros.h:85–88`）。在深层SPI等待被Motor抢占时，保存现场可能覆盖哨兵，触发全局停机、UART/CAN服务消失。

已有检测可限制一部分失控后果，但不能让业务继续运行；电脑替身没有真实ARM任务栈。本轮未测高水位、未确认发生溢出，静态预算仍需目标验证，不能称“已充分验证栈”。

最小建议：后续为Storage增加有依据的余量，重新核对RAM和所有深层路径，并在扫描/扇区边界/错误分支/并发通信负载下采水位；保留溢出检测。不为省栈取消日志校验。当前Release RAM17032/20480B是静态链接用量，不是运行期空闲堆。

### R3 — P2：ESP32 20ms发送期限没有覆盖可能阻塞的write

**证据类型：SDK故障注入、硬件待验证；正常路径未证实失效。** `esp32/main/uart/robot_uart_link.c:287–292`先uart_write_bytes，再uart_wait_tx_done(20ms)；`:518–520`安装UART的TX ring大小为0。

[ESP-IDF 6.1官方UART文档](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32/api-reference/peripherals/uart.html)说明此模式写入可能阻塞。本机同版`components/esp_driver_uart/src/uart.c:1646,1675`等待TX mutex/FIFO semaphore使用portMAX_DELAY。若TX不推进，或FIFO不足时负责释放TX等待的事件未到达，唯一link发送任务可能在write阶段不返回，无法处理后续STOP/故障通知，也到不了后面的20ms检测。

当前帧≤42B、关闭流控、只有一个发送者，普通路径不能据此断言卡死。已有STOP重试不能解除它所依赖的任务阻塞；STM32命令期限也依赖Motor正常调度，不能覆盖R1。

最小建议：在现有发送者内采用受限FIFO写入/全程deadline及明确错误恢复；单加TX ring仍有满环等待，不能自动解决问题。不增加任务。

验证：已读正式SDK源码/说明和实际配置，未注入真实发送不推进；现有UART startup替身返回立即结果，不验证SDK内部信号量。未来通过标准：TX故障后调用在约定总期限内退出、错误计数/离线可见、后续STOP与恢复策略继续有界；最终STM32停车单独测量。

### R4 — P2：特定I2C BUSY锁死没有总线恢复路径

**证据类型：硬件待验证；没有该板故障证据。** 位置为`stm32/Core/Src/i2c.c:41`的启动初始化、`sensor_task.c:80–99`及`display_task.c:105–109`的设备重试；HAL的等待边界见`Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_i2c.c:7323`。

[ST ES096 Rev15 §2.8.7](https://www.st.com/resource/en/errata_sheet/es096-stm32f101x8b-stm32f102x8b-and-stm32f103x8b-mediumdensity-device-limitations-stmicroelectronics.pdf)记录上电/ESD可使模拟滤波器状态与实际引脚不符，BUSY阻止启动主机事务，单纯SWRST/外设或系统复位不足以解除。当前只重试MPU/OLED初始化，未实现官方GPIO边沿恢复序列。

影响是两类I2C外设持续不可用；HAL时基正常时的超时、有效位、错误反馈可以降级，但不能清掉总线锁死。未连接外设的普通NACK、HAL_BUSY/超时注入，均不能证明就是该errata。

最小建议：确认芯片revision、BUSY与SCL/SDA实测后，在现有mutex下实现ST规定的有界恢复，不盲目调整频率或清错误位。通过标准：移除故障条件后恢复事务与有效数据；恢复失败仍可见，控制与STOP服务保持有界。

## 验证执行结果与复现命令

以下都是本轮结果，不引用历史CI冒充本次运行。

| 项目 / 执行目录 | 命令 | 本轮结果与限制 |
|---|---|---|
| Git / 主工程 | `git status --short`、`git rev-parse HEAD` | 开始干净，基线163dafa；结束新增3个审计文件，未提交/推送 |
| 原25组 / 主工程 | `python -u software_checks/run.py` | 沙箱内lld-link Permission denied；获批沙箱外严格编译全部DLL，加载ultrasonic.dll时4551，退出1；C断言未开始，**不能写25组PASS** |
| 新最小复现 / 主工程 | `python -u software_checks/logic/clock_repro.py` | 获批沙箱外严格编译，clock_repro.dll加载4551，退出1；未得到运行复现结果 |
| 加载边界Python检查 / 主工程 | `python -u software_checks/startup/host_loader_checks.py` | PASS；仅Python/OS错误呈现边界，不代表固件逻辑或25组通过 |
| STM32 Debug / stm32 | `cmake --preset Debug`、`cmake --build --preset Debug` | 配置成功、ninja no work to do；本轮为增量确认，没有重新编译Debug全部源 |
| STM32 Release / stm32 | `cmake --preset Release`、`cmake --build --preset Release`；随后`cmake --build --preset Release --clean-first` | 增量成功；为核对栈又执行全量重建并链接成功，FLASH50592/65536B，RAM17032/20480B；没有编译warning |
| ESP32 / esp32 | 已有6.1环境中`idf.py build` | 配置、1166步构建及镜像生成成功，app0x7b050/1MiB；有Git目录ownership警告、SDK Kconfig提示及5项私有include警告，未更改safe.directory或SDK |
| 文件检查 / 主工程 | `git diff --check`、新增Python/C检查 | 无格式错误；新C严格编译成功，Python语法/加载边界正常；没有运行新C断言 |

本机Python为`C:/Espressif/python_env/idf6.1_py3.11_env/Scripts/python.exe`，ARM GCC14.3.1、ESP-IDF6.1及其GCC15.2.0。本机激活/PATH/CMake完整命令继续使用software_checks/README.md的示例，无依赖安装。原入口加载全部DLL后才执行断言，因此不能把前几个DLL成功加载计成检查通过。WinError4551是测试环境阻塞，不是本轮新增固件bug；未绕过或关闭保护。

Release静态证据生成：

```powershell
# 在stm32执行（CMake、ARM GCC、Ninja已在PATH）
cmake --preset Release
cmake --build --preset Release --clean-first
# 在主工程执行
rg --no-ignore 'StorageTask_Entry|StorageLog_Append|InspectSector|PrepareSector|W25Q64_Read' stm32/build/Release -g '*.su'
arm-none-eabi-objdump -d stm32/build/Release/robot.elf
arm-none-eabi-nm stm32/build/Release/robot.elf
```

本轮Release全量日志在忽略目录`software_checks/build/review_stm32_release_20261007.log`，ELF反汇编为同目录`review_release_disassembly.txt`；持久结论在本文，不依赖生成日志一直存在。

## 完整链路与覆盖范围

启动：STM32 HAL/时钟→GPIO/DMA/各外设→PVD→CMSIS Ready→AppTasks静态资源和7任务→调度器。任一资源失败走Error_Handler，默认任务退出。ESP32协议自检→UART队列/驱动/两任务成功→BLE NVS/Host/Status生命周期；失败收尾和停止worker期限已读取。

控制/停止：BLE授权及严格10B校验→DRIVE拒绝/异步STOP→ESP32唯一UART发送任务→DMA/IDLE/TC→STM32 Comm帧/语义→仲裁→最新单槽运动+独立STOP位→Motor周期→驱动。CAN经同一仲裁，非零拒绝、旧STOP全局生效。PVD/核心Fault/RTOS钩子有独立EmergencyStop入口，全部实际输出写入口已检索；未确认新旁路。

反馈：Sensor/Battery/Motor→短临界区RobotState→UART STATUS/0x82/0x83和CAN导出→ESP32缓存与握手/新鲜度→加密GATT状态及CAN读取/日志。ACK只说明受理；PWM是软件施加值，均不是机械停车证明。

| 检查维度 | 本轮静态核对内容 | 除已列问题外的结果 / 边界 |
|---|---|---|
| 启动/资源 | STM32/ESP32创建顺序、失败收尾、默认线程、复位前GPIO初始化、动态失败钩子 | 在本次范围内未发现其他确认缺陷；真实NVS失败、内存耗尽、复位前STBY电气状态未验 |
| 并发/实时 | STM32全部7业务任务、队列覆盖/STOP位、I2C mutex继承/持锁、ISR通知；ESP32 portMUX、Host/Status/worker退出顺序 | 未发现已确认的锁顺序死锁、BLE退出UAF或新增STOP通知遗漏；未执行SMP/抢占/IRQ洪泛，不宣称绝无阻塞 |
| RTOS配置 | STM321000Hz、ESP32100Hz，CMSIS/IDF栈字节，原生水位word×4、周期超期处理、ISR5/PVD4、TIM4 HAL时基 | 在本次范围内未发现其他单位/FromISR错误；调度抖动、水位与异常时基未实测 |
| 内存/数值 | 27个STM32应用/自编驱动及9个ESP32C、帧/数组容量、端序/符号、固定移位宽度、ADC和/分压换算、Flash减法地址/页边界、序号回绕 | 在本次范围内未发现其他确认越界/损坏缺陷；无全工程形式化证明/动态sanitizer数据，厂商库只沿相关路径读取 |
| UART/BLE/CAN | CRC/长度/噪声恢复/半帧、循环DMA写位置和重复事件、软件环溢出、ACK与STOP握手、CAN独立STOP槽/FIFO预算/bus-off、BLE授权/换绑/Host退出 | 在本次范围内未发现其他确认互通错误；真实DMA覆盖/回调次序/PHY/RF/密码学尚未验 |
| 状态/时效 | 300ms命令、500ms会话/状态、采样时间与有效位、清过期导出、owner/模式/换手、新命令恢复 | F1已列。STATUS新鲜度采用本机接收时间，不是远端采样时间；STATUS序号未用于反重放，未证实正常链重复旧状态，故不新增确认缺陷 |
| 控制/驱动 | 全部控制提交者和最终输出入口；电池资格、PVD锁存、换向死区、超时、ADC迟到回调、HC中断、I2C/SPI错误收尾 | F1、R1～R4以外在本次范围内未发现其他确认缺陷；PVD响应/电气校准/机械制动未验 |
| 配置/诊断/检查 | CMake27/9源、Debug/Release开关、CI6.1基线、诊断0x82修订2/0x83、新旧错误清理、现有25组边界替身 | F2已列；健康只诊断、IWDG未启是已知限制。测试成功不证明实机，且本轮C断言被环境拦截 |

外设一致性核对：USART1 115200/8N1、DMA1 Ch5 BYTE/CIRCULAR；ADC1 Ch1 HALFWORD/NORMAL、软件单次；TIM2 PSC71/ARR65535按72MHz为1MHz；TIM3 ARR3599为20kHz；CAN APB1 36MHz/(4×18)=500kbps；I2C1 PB6/7 100kHz；SPI2 PB13/14/15 4.5MHz，CS PB12；NOJTAG释放相关GPIO。以上由配置计算，均非示波器实测。

排除两类误报：ADC的HAL uint32_t指针参数不改变DMA HALFWORD数据宽度；MPU配置0x08为±500dps，raw×2000/131与量程一致。STM32 UART OVERFLOW后保留最新数据并继续解析，已有源撤销/STOP位/guard配合；未证实违反新命令规则，不单独列缺陷。

测试边界：临界区替身为顺序执行，未模拟抢占/优先级反转；ADC/DMA Abort和HAL错误替身不证明真实迟到IRQ；BLE不模拟NimBLE密码学/RF。当前SDK_USE_ESP_TIMER=y的NPL毫秒转换与BLE替身n/10不同，现有用例未验证真实广告期限，不把它当生产计时bug。任务创建替身不实际耗尽内存。

未覆盖：本轮没有完整MCU仿真、目标RTOS运行、SWD/串口设备访问、全厂商库独立审计、动态内存/竞态sanitizer、手机实测和长稳测试。原因分别是本轮软件范围、当前应用控制阻塞和缺少硬件/运行数据。不能把历史CI或设备观察补成当前这些验证。

## 下一步修复顺序（本轮不实施）

1. 开放任何运动之前解决/定义R1独立失效停机和复位安全标准；这不等于立即启用运动。
2. 修F1，在统一状态/健康保护边界处理时间快照；在可信环境执行新复现并改为修复后回归，覆盖正常回绕与全部消费者。
3. 为R2调整Storage栈预算，重查RAM；真实RTOS采高水位/故障路径。不要仅凭编译通过验收。
4. 对R3建立全程UART发送期限及故障恢复；用正式SDK验证故障下阻塞上限。
5. 确认R4实际故障及器件revision后补有界I2C恢复；正常NACK与errata分开。
6. 对齐F2文档/常量/检查；若无新需求，按当前30秒描述即可。

## 硬件待验证与可观察通过标准

这些是后续授权清单，本轮没有执行；未定义时间上限的项目先定义预算。

| 项目 | 可观察通过标准 |
|---|---|
| 上电/复位/PVD | 执行器先隔离；STBY启动/复位保持禁用，PVD触发输出关闭且不能自动重新启用；采电源与引脚波形，不仅看ACK |
| 关键任务停摆 | 在隔离执行器的授权环境注入停摆，独立关闭/安全复位在约定上限内发生；恢复不沿用旧命令。当前R1标准未满足证明 |
| RTOS栈/并发 | 扫描、扇区边界、错误分支及通信负载下记录所有栈水位，留预定余量；无溢出钩子/健康假锁存；PSP及现场佐证R2 |
| UART DMA/发送恢复 | 拆包/粘包/噪声、持续数据、延迟IRQ及收发故障后有界恢复；无旧命令复活，错误计数可见；R3总发送期限实测 |
| CAN洪泛/bus-off | STOP优先消费、有限ISR/任务预算下Motor继续推进；bus-off后有界重建，新命令/诊断语义一致 |
| BLE生命周期 | 首绑/旧绑/换绑、未加密期限、断连缺回调、Host reset/停止超时、GATT读取通知分别测；DRIVE仍拒绝；失败能STOP请求/离线反馈 |
| ADC/测距/I2C/SPI | 真实超时/迟到事件不混入新一代数据；无效位保持诚实；I2C锁死证据与恢复匹配，SPI CS收尾/Flash读回CRC正常 |
| Flash掉电/日志 | 断写/重启扫描不接受坏CRC或破坏保留区，扇区与序号回绕正确；既有电脑NOR替身不是掉电验收 |
| 实际停车/长稳 | 未来另行授权后测软件关闭与机械停止各自时间、误差、负载、堆/栈趋势；本项目当前没有该证据 |

## 本轮新增文件

- `docs/EMBEDDED_REVIEW.md`：完整发现、证据、执行结果与剩余验证。
- `software_checks/logic/clock_repro_checks.c`：直接链接生产时间消费者的最小交错检查。
- `software_checks/logic/clock_repro.py`：复用现有Windows检查工具/错误边界的独立复现入口。

原有受Git跟踪的生产、配置、文档、统一检查入口均未改；构建生成物留在忽略目录。未提交/推送这些新增文件。
