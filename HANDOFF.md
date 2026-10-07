# STM32 + ESP32 双主控机器人交接

更新：2026-10-07。只进行软件检查、固件构建和Git交付，不操作开发板。历史设备观察及限制在docs/ENGINEERING.md第10节；当前验证在software_checks/README.md。

## 1. 现在做什么？

用户最新选择“先只完善错误提示和交接记录”。本轮只改主机检查的DLL加载提示和记录，不安装Linux、不更改Windows策略；之前的驱动/BLE补测仍保留待最终运行状态。唯一主工程为本仓库；远程origin为https://github.com/xksszm-ux/water.git，分支main。已有本地提交022c551/68047d3/e921443，最新以git log/status为准；继续保留本地，不推送GitHub，CI未启动。

## 2. 已经完成了什么？

- 双端UART V1、STOP优先与旧命令失效、控制仲裁、过期数据、重连握手、0x82诊断及0x83 CAN反馈已经形成软件链；BLE DRIVE、CAN/AUTO非零运动仍拒绝。
- 按职责迁移45个文件，构建及include引用同步；STM32有27个应用/自编驱动C文件，ESP32有9个C文件。ESP32基线为官方ESP-IDF 6.1，tick为10 ms；STM32为1 ms。
- 原20组检查保留，新增PowerMotorChecks_Run、AdcDmaChecks_Run、UltrasonicChecks_Run、I2cSpiChecks_Run、EspBleChecks_Run，合计25组。新增五组初版已分别运行返回0；ADC/超声波/SPI扩展边界已包括在该独立结果中。随后追加的GATT新鲜反馈、STOP通知失败等小用例仍等待最终复测，详见验证记录。
- 实际复现MPU6050_Init(NULL)在先前成功后保留ready，已在共同初始化入口先清ready，回归通过；生产调用者均已核对。不是实机故障经历。
- 本次STM32 Debug/Release配置、重新链接成功：FLASH 59028/50592 B，RAM 17048/17032 B。ESP-IDF 6.1增量构建成功，app 503888 B，bootloader 26176 B。构建不是硬件验收。
- 检查优先采用PATH中的LLVM，可用ROBOT_HOST_CLANG/ROBOT_HOST_LINK覆盖；旧本机路径仅作fallback。STM32 preset移除固定工具目录，使用PATH ARM GCC/Ninja；实际本机构建已验证。
- 已添加.github/workflows/software.yml：Windows主机检查、STM32 Debug/Release、ESP-IDF 6.1自动构建；尚不能仅凭文件存在宣称CI通过。
- DLL加载入口对4551显示BLOCKED、目标路径及“运行检查尚未开始”，失败退出，不跳过检查或生成PASS。仅加载期间用SetThreadErrorMode加SEM_FAILCRITICALERRORS，finally恢复旧线程模式，防止Bad Image弹窗挂住CLI；不改系统策略。独立Python回归startup/host_loader_checks.py覆盖原有模式位、成功/4551/126/中断的恢复、设置/恢复失败及主异常优先，已通过；真实被拦DLL也直接返回BLOCKED/退出1，不需点击新弹窗。这些不是25组生产C验证。

## 3. 卡在哪里？

- 本机Windows CodeIntegrity事件3077确认应用控制策略间歇拦截新DLL，返回WinError 4551；未更改签名策略/防护设置。全部检查C源码在最后一轮扩展之前已以-Wall -Wextra -Werror编译，五组新增检查独立通过，但最终统一入口没有完成。
- 用户管理员PowerShell截图确认所有DLL编译完成后在app_tasks_init.dll加载处失败；本轮单独ctypes.CDLL加载同路径又复现4551。Authenticode显示NotSigned，3077记录同文件的策略拒绝；具体生效策略读取被拒绝，不能仅凭通用日志断言是企业策略或Smart App Control。新提示的注入回归通过，实际系统阻止没有解除。
- 新截图Bad Image状态0xC0E90002为STATUS_SYSTEM_INTEGRITY_POLICY_VIOLATION；已用系统RtlNtStatusToDosError确认映射4551，通用“损坏的映像”标题不能替代状态码判定。真实加载已不再等待弹窗，但应用控制仍拒绝DLL，最终C运行缺口保留。
- 用户已于10-07明确批准软件复测。批准后沙箱外复测及允许的一次重试仍被自动审批超时拒绝，均未启动脚本；保持原沙箱限制运行则在lld-link返回Permission denied，连其--version也失败。不存在等待用户再次批准的问题，当前是执行环境阻塞；没有新的运行通过结果，不无限重试或绕过策略。
- GitHub默认沙箱网络无法连接443；沙箱外正常push的自动审批也连续两次超时，没有启动实际推送。随后用户选择先保留本地提交，当前推送停止，不能把本地提交当作远程交付。GitHub连接器读取也曾报HTTP传输失败。
- 真实RTOS抢占、多核互斥、NVIC/DMA时序、NimBLE协议栈/加密/RF、物理I2C/SPI恢复、PVD响应、栈峰值、CPU负载、机械停车及长稳仍未验证。
- 功能缺口仍有完整手机诊断、日志导出、任务健康故障处置策略；BLE运动、避障、编码器PID和OTA未实现。它们不属于本轮补测范围。

## 4. 下一步做什么？

1. git status --short，读本文件、README、docs/ENGINEERING.md及software_checks/README.md。软件复测已获批准，但用户自己的管理员终端也被系统策略拦截，重复提权/重跑不能当作解决方案。当前仅完善提示与记录；待后续明确选择合规可执行的测试环境，再运行最终25组。保留Windows保护，不访问hardware脚本。
2. 核对所有交付路径、文档链接、生成物/敏感文件排除及git diff --check。已有未提交的分类迁移和源码一起作为主工程交付，保留上级副本。
3. 本仓库已提交；用户当前要求保留本地，只有后续明确恢复上传才推送origin/main，不使用force。首次fetch时HEAD与origin/main相同，新增本地提交后尚未上传；届时推送前后再核对，检查GitHub Actions实际作业，发现失败修复并重跑，再更新持久记录。
4. 后续软件开发先选择手机完整诊断、日志导出或任务健康处置之一，沿生产链补小检查；运动/导航/PID/OTA另行定义范围。IWDG继续关闭，健康期限仍是静态预算。

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

开发遵守上级AGENTS.md及项目Ponytail技能：先追生产调用链，再最小改动，不新增框架/依赖来展示术语。测试包含实际.c，仅替换硬件/OS边界；新BLE检查执行生产回调和Host/Status/Stop任务体的顺序路径，不模拟NimBLE密码学或真实抢占。SDK6.1的nimble_port_stop可能无限等待，必须保留隔离worker与超时后阻止重启的策略。

复现命令从主工程根目录执行，先将ARM GCC/Ninja加入PATH并激活ESP-IDF6.1；详细本机示例在software_checks/README.md：

```powershell
python -u software_checks/run.py
./verify.ps1
```

单独固件构建：stm32目录cmake --preset Debug / cmake --build --preset Debug（Release同理）；esp32目录idf.py build。生成物在各build目录并被Git忽略。GitHub工作流是无硬件的独立环境验证；可移植构建结果、运行URL与真实限制必须按实际结果补记。
