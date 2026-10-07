# STM32 + ESP32 双主控机器人交接

更新：2026-10-07。审计F1/F2已按用户要求依次修复；本地新改动尚未提交/推送。项目仍未完成、未通过硬件验收，不操作开发板。历史设备观察见docs/ENGINEERING.md第10节，最新结果见software_checks/README.md。

## 1. 现在做什么？

用户最新要求先解决审计F1共享旧时间误判，再解决F2加密期限文档冲突。AppTasks健康及RobotState传感/电池过期、运动电源资格判断现于各自临界区内取tick，公共接口不再接收外部now；全部调用者同步。BLE文档与现有30秒实现一致，没有改变蓝牙策略。

修改前旧复现实际运行，确认假健康位0x02及新样本误清；修改后单独执行AppTasksInitChecks_Run和ClockRepro_Run均返回0。完整26组严格编译完成，但统一入口加载checks.dll时4551，尚未执行全套断言。STM32 Debug/Release重新编译链接成功，ESP-IDF6.1增量构建成功；没有硬件操作。

GitHub已上传基线仍为163dafa（源码09c4869），远程https://github.com/xksszm-ux/water.git；其CI37584729464的旧25组及两端构建成功是历史基线证据，不能代替本次26组。保留审计前3个未跟踪文件，未提交/推送本轮改动。

## 2. 已经完成了什么？

- 双端UART V1、STOP优先与旧命令失效、仲裁、数据过期、重连握手、0x82诊断及0x83 CAN反馈形成软件链。BLE DRIVE、CAN/AUTO非零运动继续拒绝。
- 45个文件已按职责分类，构建/include同步；STM32有27个应用/自编驱动C文件，ESP32有9个。基线ESP-IDF6.1，STM32/ESP32 tick分别1/10 ms。
- 原20组与5组新生产驱动/BLE检查共25组，最终版本在Windows2022 CI完整通过，包括最后的GATT新鲜反馈、STOP通知失败及Host退出用例。顺序HAL/OS替身不证明实际RTOS/外设效果。
- MPU6050失败初始化残留ready已电脑复现、修复及回归。PVD/电机、ADC DMA、超声波、I2C/SPI边界与完整BLE源码事件检查均保留生产逻辑。
- CI实际完成STM32 Debug/Release和ESP-IDF6.1全新构建，工具链/尺寸/警告统一记录在验证README；ARM GCC13.2.1 CI与本机14.3.1产物分开，不宣称位级一致。
- PATH工具选择、固定路径移除、工作流以及完整源码/测试/说明已提交并推送；构建产物与本机sdkconfig被忽略。
- DLL加载4551显示BLOCKED、路径及未开始运行，失败退出。只在加载期间设置线程SEM_FAILCRITICALERRORS并finally恢复，防止Bad Image弹窗挂起；Python诊断回归和真实阻止返回已验证，不更改系统策略、不计入25组C检查。
- F1两组生产回归已运行通过：进入保护前的Motor/采样更新、计数回绕、200/201ms传感和1500/1501ms电池边界、真实Motor超期与锁存；不是实际抢占/ISR运行。旧clock_repro现为修复后回归，退出0表示通过，不再表示旧bug被复现。统一入口新增该组，现为26组。
- F2只更新STEP10_BLE_PROTOCOL.md的10→30秒说明，保留现有约200ms周期检查和GAP断连确认。

## 3. 卡在哪里？

- 本机仍有CodeIntegrity/4551与沙箱lld-link权限问题，历史审批也曾超时；本次完整26组在checks.dll加载阶段被阻止。独立构建clock_app_tasks.dll也被阻止；随后单独执行统一入口生成的app_tasks_init.dll和clock_repro.dll通过，不能据此断言系统策略解除。旧CI25组不覆盖本次API/检查修改。
- 截图0xC0E90002经微软定义和RtlNtStatusToDosError确认对应4551；DLL签名NotSigned、3077同路径拒绝。具体策略读取被拒绝，不能断言是企业策略或Smart App Control。CI成功不意味着本机DLL已放行。
- 首次上传本地领先5个提交、远程无新提交，正常push成功。匿名REST读取曾受共享IP限流，后通过已连接GitHub工具取得实际CI作业/日志；该限流不影响已完成的CI结果。
- 真实抢占/多核、NVIC/DMA时序、NimBLE/RF/密码学、物理总线/采样精度/PVD响应、栈峰值、CPU负载、机械停车和长稳仍未验。
- 完整手机诊断、日志导出、健康故障处置仍部分完成；BLE运动、避障、编码器PID、OTA未实现，IWDG未启用。

## 4. 下一步做什么？

1. git status --short，读本文、README、工程说明及验证README，保护未提交/未跟踪改动。在获准环境运行现有software_checks/run.py完成本次26组；未经新授权不自动推送或处理Windows策略，不把历史CI当作新版本结果。
2. F1/F2实现已处理，真实RTOS仍待验；其余审计R1～R4尚未修改。下一轮先明确关键任务停摆处置及Storage栈风险的范围，再推进诊断/日志等功能；运动/导航/PID/OTA另行定义。继续保留STOP-only，健康期限是静态预算。
3. 本机App Control故障单独保留，只有用户明确要求处理环境才继续；CI运行不要求关闭本机保护或安装Linux。不运行hardware脚本，保留上级副本。

## 5. 哪些坑不要踩？

- 不把构建、顺序HAL/OS替身、零PWM/ACK当作硬件验收或机械停车；不编造CPU数字、长稳时长或实机bug。
- 不清0x5E、不调整电池阈值、不伪造正常采样。用户只连接电机驱动与5 V→3.3 V降压，PA0什么都没有接，其余传感/显示/存储/CAN外设未接；历史约3.2 V不是有效电池实测。
- 不开放BLE DRIVE/CAN/AUTO非零运动；UART内部运动路径仍存在，不能称全局禁用运动。故障恢复仍需新命令，保留资格/校准参数。
- UART IDLE不是帧边界；重复发布不能刷新采样时间。ISR不做阻塞/复杂解析；PVD优先级4不调用RTOS，FromISR外设优先级5满足内核约束。
- CMSIS/ESP-IDF栈创建参数按字节，上游FreeRTOS常按word；挂起调度器不关闭中断，也不替代ESP32跨核锁。
- 不执行software_checks/build里的live/boot/SWD脚本，不混用上级robot/esp32/software_checks副本，不用reset/clean清理未提交文件；不上传构建产物或本机生成sdkconfig。

## 6. 有哪些必须保留的关键上下文？

STM32拥有最终输出/停止权，7个常驻业务任务；AppTasks_Init在调度器启动前创建资源，失败进入Error_Handler。默认任务启动后退出，静态栈/数组不回收。ESP32先协议自检→UART启动→BLE；UART两任务都创建成功后才通知启动，不保证并发Start/Get/清理安全。

控制命令300 ms过期；独立STOP位优先清单槽最新命令队列。CAN共享Submit会消费前向序号但安全拒绝非零请求，不取owner/不入队；旧CAN STOP仍全局生效。AUTO目前只是CAN控制域，PID包解析后仍UNSUPPORTED，编码器引脚在现有EXTI回调被忽略。

ESP32 ONLINE必须有本轮序号匹配STOP ACK及严格晚于ACK的新鲜零输出STATUS，同tick保守等下一帧。STATUS新鲜500 ms，STOP重试250 ms；诊断不推进握手。0x82诊断为revision2，0x83 CAN反馈独立revision1/16 B，BLE ...1003加密只读18 B；离线或报告超3000 ms不可用。报告新鲜与事件age不同，最新原因可被随后STOP覆盖，无逐命令CAN运动ACK。

健康策略：3 s启动宽限，Motor100 ms、Sensor/CAN/Comm1000 ms、Battery2000 ms、Display5000 ms；Storage因长扫描/擦写不设超时。仅诊断锁存，不独立健康停机/喂狗，IWDG关闭。ADC每通道丢首样+8样，共18次各自等待20 ms，不是整个读取20 ms。PVD/电机紧急锁存保持保护；电池/方向/分压参数须保留并待硬件校准。

AppTasks_HealthPoll及RobotState三项时效/资格接口现在无now参数，必须在共享临界区内采当前tick；TaskHealth_Poll仍为纯逻辑，由AppTasks保证时间与心跳一起串行化。测试用现有时钟替身，不能恢复“从任务循环外传旧now”的接口。软件回归只注入保护入口前的顺序交错；不模拟真实调度。

开发遵守上级AGENTS.md及项目Ponytail技能：先追生产调用链，再最小改动，不新增框架/依赖来展示术语。测试包含实际.c，仅替换硬件/OS边界；新BLE检查执行生产回调和Host/Status/Stop任务体的顺序路径，不模拟NimBLE密码学或真实抢占。SDK6.1的nimble_port_stop可能无限等待，必须保留隔离worker与超时后阻止重启的策略。

复现命令从主工程根目录执行，先将ARM GCC/Ninja加入PATH并激活ESP-IDF6.1；详细本机示例在software_checks/README.md：

```powershell
python -u software_checks/run.py
./verify.ps1
```

单独固件构建：stm32目录cmake --preset Debug / cmake --build --preset Debug（Release同理）；esp32目录idf.py build。生成物在各build目录并被Git忽略。GitHub工作流是无硬件的独立环境验证；可移植构建结果、运行URL与真实限制必须按实际结果补记。
