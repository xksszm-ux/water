# 软件验证记录

更新：2026-10-07。项目未完成，未通过硬件验收。当前入口26组，源码0de6f2a的完整CI已通过，见下文“10-07 F1/F2交付CI”；旧25组CI与20组数字为历史记录，不扩大成真实RTOS/硬件验证。本轮不访问串口、ST-Link或板卡。历史设备观察见[工程说明](../docs/ENGINEERING.md)第10节。

## 唯一入口与复现

在主工程根目录执行；路径示例来自本次实际环境：

```powershell
. 'C:/Espressif/tools/activate_idf_v6.1.ps1'
$env:IDF_CCACHE_ENABLE='0'
$env:PATH='C:/Users/12992/AppData/Local/stm32cube/bundles/gnu-tools-for-stm32/14.3.1+st.2/bin;C:/Users/12992/AppData/Local/stm32cube/bundles/ninja/1.13.2+st.1/bin;'+$env:PATH
./verify.ps1 `
  -Python 'C:/Espressif/python_env/idf6.1_py3.11_env/Scripts/python.exe' `
  -CMake 'C:/Users/12992/AppData/Local/stm32cube/bundles/cmake/4.3.1+st.1/bin/cmake.exe'
```

`verify.ps1` 依次运行主机测试、STM32 Debug/Release配置与构建、ESP32构建；任一步失败即终止。默认参数为PATH中的python/cmake，需要已激活的idf.py和PATH中的ARM GCC/Ninja。STM32 preset不再绑定本机工具目录，上述PATH只是本机示例，换机器使用自己的安装目录。

单独运行电脑测试：

```powershell
& 'C:/Espressif/python_env/idf6.1_py3.11_env/Scripts/python.exe' -u software_checks/run.py
```

脚本使用环境覆盖→PATH LLVM→旧本机MATLAB Clang/ST lld-link fallback的顺序选择工具，编译实际C源码为Windows DLL，通过Python ctypes运行。无新Python依赖。可设置 `ROBOT_HOST_CLANG` / `ROBOT_HOST_LINK`；覆盖值无效时直接失败，不偷偷换工具。中间文件在 `software_checks/build/`，不纳入Git。主机检查仍是Windows x64入口，未宣称Linux/macOS可直接运行。

本机沙箱不允许运行lld-link，本次获批后在沙箱外执行。首次ESP32沙箱构建有Git目录所有权警告；最终统一入口在授权环境成功。没有改全局Git safe.directory、没有安装新依赖。

## 检查代码分类

`run.py` 保留原入口。检查按职责放在 `logic/`（控制与策略）、`protocol/`（互通/编码）、`integration/`（通信调度与连续双端场景）、`drivers/`（驱动及日志边界）、`startup/`（资源创建失败）中。`stubs/`、`startup_stubs/`、`driver_stubs/` 保留原位置，分别提供不同检查需要的边界声明；ESP32 UART启动声明与其检查放在 `startup/`。生产源码的显式引用与编译include目录已同步调整，不复制生产算法。

## 本轮实际结果

### 10-07 F1/F2交付CI（源码0de6f2a）

用户授权使用现有CI后，核对远程main仍为163dafa，正常推送修复提交`0de6f2aca682011756dd0de03d27697b1056fb8f`，包含生产时间保护、全部调用者、回归及原3个审计文件。没有修改工作流、安装本机依赖、关闭安全设置或操作硬件。

[运行37615888159](https://github.com/xksszm-ux/water/actions/runs/37615888159)是该提交的push/attempt1；北京时间19:42:22启动、19:45:25更新为completed/success，四个作业日志已逐一读取。不是旧25组结果，也不只看工作流配置。

| 本次实际CI作业 | 结果与环境 |
|---|---|
| [host-checks](https://github.com/xksszm-ux/water/actions/runs/37615888159/job/112773986716) | Windows2022、CPython3.11.9、镜像LLVM；全部DLL以-Wall/-Wextra/-Werror编译，26条PASS和Software checks passed，包含ClockRepro_Run及扩展的AppTasks_Init保护边界 |
| [STM32 Debug](https://github.com/xksszm-ux/water/actions/runs/37615888159/job/112773986646) | Ubuntu24.04、ARM GCC13.2.1，全新配置/构建成功；FLASH59804/65536 B、RAM17048/20480 B |
| [STM32 Release](https://github.com/xksszm-ux/water/actions/runs/37615888159/job/112773986424) | 同一CI工具链，全新配置/构建成功；FLASH51452/65536 B、RAM17032/20480 B |
| [ESP32](https://github.com/xksszm-ux/water/actions/runs/37615888159/job/112773986793) | 官方ESP-IDF v6.1容器、GCC15.2.0，全新构建成功；app503888 B（0x7b050）/1 MiB、bootloader26176 B（0x6640） |

STM32 CI13.2.1与本机14.3.1数字分别记录，不宣称位级一致或性能提升。ESP32仍有5项SDK内部CMake依赖警告，Actions有Node弃用提示；没有修改SDK或屏蔽警告。CI运行现有run.py，未另行执行独立clock_repro.py或host_loader_checks.py；26组是生产逻辑检查组数，不是26个硬件用例。

本机只读核查：VerifiedAndReputablePolicyState=1，19:22:31的checks.dll CodeIntegrity事件3077记录PolicyName=VerifiedAndReputableDesktop、Status=0xc0e90002，DLL为NotSigned，已确认Smart App Control处于执行模式。此前“具体策略未知”保留为当时读取受限的历史记录；不能据此推断启用日期。CI成功补齐运行证据，不解除本机4551。

后续push/PR自动运行`.github/workflows/software.yml`；也可在仓库Actions → Software verification → Run workflow选择分支执行。每次核对head_sha及四个作业日志；本次通过不覆盖未来改动。本轮验证记录另作仅文档提交（`[skip ci]`），不重复构建相同生产源码/检查，被测版本仍为0de6f2a。

电脑HAL/OS/NimBLE替身仍是顺序验证；实际RTOS抢占、多核、IRQ/DMA时序、栈峰值、RF/密码学、电气及机械停车均未由CI验证。BLE DRIVE拒绝、CAN/AUTO非零拒绝、UART内部路径与全局STOP保持原状，健康监管仍为诊断锁存，IWDG未启用。

### 10-07 F1/F2修复（本地阶段，完整结果由上节CI补齐）

按用户顺序修复共享旧时间误判及BLE期限文档。旧clock_repro.py在修改前实际运行：`REPRODUCED: healthy Motor falsely latched (0x02); newer sensor sample invalidated by older observer time.` 返回0是当时旧缺陷复现，不是修复通过。

AppTasks_HealthPoll和RobotState传感/电池过期、运动电源资格接口不再接收外部now；在短临界区内采tick并读取共享状态，保留无符号回绕和原阈值。所有固件/检查调用者同步。AppTasksInitChecks_Run扩展进入临界区前Motor抢占的顺序注入，验证健康时间/心跳快照、真实超期锁存与回绕；clock_repro_checks.c直接include生产RobotState，检查新样本抢占、200/201ms传感、1500/1501ms电池、零tick/回绕与非常旧数据。

| 本次执行 | 结果 |
|---|---|
| `python -u software_checks/logic/clock_repro.py` | 修复后严格编译完成，clock_app_tasks.dll加载4551，断言未开始；脚本现为修复后回归入口 |
| `python -u software_checks/run.py` | 当前26组DLL全部-Wall/-Wextra/-Werror编译完成，checks.dll加载4551、退出1，全套断言未开始；新组已纳入现有统一入口/CI |
| 单独执行统一入口已生成的AppTasksInitChecks_Run及ClockRepro_Run | 使用run.py同一load_check_library错误边界，两个导出均返回0/PASS，进程退出0；只替换OS/时间边界，实际RTOS/ISR未运行 |
| STM32 Debug/Release配置与增量构建 | 两者都重新编译受影响源并链接成功；ARM GCC14.3.1，Debug FLASH59008/RAM17048B，Release FLASH50576/RAM17032B |
| ESP-IDF6.1 `idf.py build` | 增量成功，app0x7b050、bootloader0x6640；ESP32生产代码未改，SDK旧警告未修 |

该本地阶段两组通过不等于26组全部通过，也不等于本机策略解除。当时没有反复重试已明确阻止的相同DLL、关闭保护、安装环境或上传本轮代码；随后新CI的26组结果见上节，历史CI37584729464仅验证旧基线。独立两模块的可复现入口如下（无需重新编译；先运行上述run.py生成DLL）：

```powershell
@'
import ast, ctypes as c, sys
from pathlib import Path
root = Path.cwd()
tree = ast.parse((root / 'software_checks/run.py').read_text(encoding='utf-8'))
loader = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'load_check_library')
exec(compile(ast.Module(body=[loader], type_ignores=[]), 'software_checks/run.py', 'exec'))
checks = [('app_tasks_init.dll', 'AppTasksInitChecks_Run'), ('clock_repro.dll', 'ClockRepro_Run')]
loaded = [(load_check_library(root / 'software_checks/build' / path), name) for path, name in checks]
for dll, name in loaded:
    check = getattr(dll, name)
    check.restype = c.c_int
    line = check()
    assert line == 0, f'{name}: source line {line}'
    print(f'PASS {name}')
'@ | & 'C:/Espressif/python_env/idf6.1_py3.11_env/Scripts/python.exe' -
```

F2只对齐STEP10_BLE_PROTOCOL.md为30秒，到期由现有约200ms状态任务请求STOP/断连，GAP确认仍是异步；代码常量与既有29999/30000ms用例不变，没有实机断连验收。

### 10-07 GitHub交付与最终CI验证

用户重新授权上传后，fetch确认本地领先5个提交、远程未新增提交，正常push将main从f6001ae更新至09c4869。GitHub运行[37584729464](https://github.com/xksszm-ux/water/actions/runs/37584729464)（push，attempt1，源码09c4869）四个作业均success；北京时间15:00:39启动、15:03:23完成。已读取实际作业日志，不仅检查工作流配置或提交状态。

| 实际CI作业 | 执行结果与环境 |
|---|---|
| [host-checks](https://github.com/xksszm-ux/water/actions/runs/37584729464/job/112672161676) | Windows2022、CPython3.11.9、镜像LLVM；生产检查-Wall/-Wextra/-Werror编译，25条PASS及Software checks passed，最后新增GATT用例已执行；无硬件/真实RTOS |
| [STM32 Debug](https://github.com/xksszm-ux/water/actions/runs/37584729464/job/112672161728) | Ubuntu24.04、ARM GCC13.2.1，全新配置构建；FLASH59824 B/65536 B，RAM17048 B/20480 B |
| [STM32 Release](https://github.com/xksszm-ux/water/actions/runs/37584729464/job/112672161909) | 同一CI工具链，全新配置构建；FLASH51448 B/65536 B，RAM17032 B/20480 B |
| [ESP32](https://github.com/xksszm-ux/water/actions/runs/37584729464/job/112672161490) | 官方ESP-IDF v6.1容器、GCC15.2.0，全新构建；app503888 B/1MiB，bootloader26176 B |

本机ARM GCC14.3.1的59028/50592 B FLASH与CI13.2.1结果分列，不能混用不同工具链数字或据此宣称优化/位级一致。本机Windows策略仍拦截DLL，独立CI成功不代表本机故障已解除；过去“最终25组未执行”是当时时点，本节补齐最终运行证据。

SDK仍有5项esp_wifi/wpa_supplicant私有include警告，CI action另有Node弃用提示；未修改SDK或屏蔽它们，不把success描述为全部日志无warning。主机C编译-Werror实际通过；单独host_loader_checks.py诊断回归是此前本地证据，CI没有另行调用它。真实抢占、多核、NimBLE/RF/密码学及硬件验收边界保留，STOP-only与保护未改。

### 10-07本地补测及阻塞历史（最终结果见上节）

新增五组（总25组）的完整C源码已编译，五组分别调用检查入口返回0，使用-Wall -Wextra -Werror。随后在BLE组追加STOP通知失败、GATT新鲜/过期反馈和意外Host退出等小用例，该最终C检查文件又单独编译成功；最终运行复测仍受下述执行环境阻塞，不能把初版结果冒充最终全部通过。run.py也通过Python语法编译检查。

| 新增入口 | 已执行的主要场景与边界 |
|---|---|
| PowerMotorChecks_Run | PVD优先级/锁存、PWM启动失败、实际motor.c钳位/混控/三种STOP、PRIMASK恢复、PVDO变化及解除屏蔽后紧急清BRR/CCR；寄存器是顺序替身 |
| AdcDmaChecks_Run | 18次采样逐点超时/启动失败/错误，停止时迟到回调、错误ADC句柄、恢复/校准/延时/通道失败及等待句柄清除；使用实际DMA流程和换算 |
| UltrasonicChecks_Run | TIM2配置/启停/停转、调用上下文、16位回绕、超时/Echo两种顺序、短脉冲/边沿预算、距离边界和任务兜底；生产ISR与Service顺序执行 |
| I2cSpiChecks_Run | MPU/OLED初始化及页/读事务失败、身份/地址、实际换算；W25 JEDEC/WEL、全部事务失败、片选收尾、分页/读取分块和忙超时/时间回绕；不模拟物理总线 |
| EspBleChecks_Run | 包含完整robot_ble.c，执行真实Start/Stop/HostTask/StatusTask及GAP/GATT回调；NVS/NPL/GATT/任务失败、广告重试、退出超时/清理锁存、授权/断连/换绑/Host reset、CRC/长度/DRIVE拒绝及通知失败；OS/NimBLE边界为替身 |

本机构建实际命令：上述PATH下从stm32执行Debug/Release配置与构建，从激活6.1的esp32执行idf.py build。均返回0：Debug FLASH59028/RAM17048 B，Release FLASH50592/RAM17032 B；ESP32增量app503888/bootloader26176 B。未访问硬件，ESP32没有全量重编译。

本机统一入口先编译全部DLL再加载，曾在app_tasks_init/power_motor/drivers.dll处被Windows CodeIntegrity事件3077拦截，Python报WinError4551；独立五组读取成功不代表统一入口成功。没有关闭防护、修改签名策略或换文件名躲避审核。

用户10-07明确批准后，重跑software_checks/run.py仍遇自动审批超时；按工具允许重试一次也同样超时，两次均未创建运行进程。尝试保留沙箱限制运行相同命令，则在第52行生成crt.lib前由lld-link返回Permission denied（退出1）；单独lld-link --version也失败，未观察到本次新的CodeIntegrity事件3077，不能把当前拒绝归因于旧DLL签名拦截。没有本次25组PASS或最终GATT运行结果，拟保存的final_retest_20261007.log因首条命令未执行而不能作为证据。授权已明确，当前是审批服务/执行环境阻塞，不要求用户重复批准。

最后追加用例已再次对照生产CommandAccess/StatusAccess/CanFeedbackAccess/HostTask及状态编码字段，未发现明显预期冲突；这是静态核对，不是执行通过。新鲜/过期UART快照由替身返回，因此该GATT组验证读取与载荷呈现，不重新证明真实接收任务或500/3000 ms过期计时；这些生产策略另有既有检查，真实链路仍待验。

用户随后在管理员PowerShell自行运行，截图显示全部DLL编译成功，但加载app_tasks_init.dll时返回4551。本轮最小复现命令为 `python -c "import ctypes; ctypes.CDLL(r'C:/Users/12992/Desktop/Experiment1/stm32-esp32-freertos-robot/software_checks/build/app_tasks_init.dll')"`（实际使用上述SDK Python路径），返回相同4551。Authenticode检查为NotSigned，CodeIntegrity事件3077记录同路径拒绝；[微软诊断说明](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/app-control-for-business/operations/appcontrol-debugging-and-troubleshooting)将3077定义为生效策略阻止事件。读取具体策略的CiTool查询返回Access denied，不能据此推断实际策略类型。

按用户“先只完善错误提示和交接记录”的要求，所有9个库统一走生产load_check_library：4551输出BLOCKED/完整路径/尚未开始运行检查并退出1，其他OSError保持原样。新增Python检查直接从run.py抽取该生产函数，只替换OS加载边界。运行命令为 `python software_checks/startup/host_loader_checks.py`，无需Clang或外设；这是工具诊断回归，**不增加25组C检查的通过数**。

新加载提示的真实DLL尝试没有及时完成，已中止该本轮诊断进程，不能把空的loader_diagnostic_20261007.log作为运行成功证据；确定的真实复现来自前述最小命令与用户截图，确定的提示验证来自Python注入回归。未修改系统安全设置、签名信任或固件逻辑，未安装WSL/Docker，未新增Linux检查入口，未上传GitHub。合法测试环境/签名策略处理须后续明确范围，当前系统阻止及最终25组运行缺口仍保留。

随后用户提供python.exe“损坏的映像”弹窗，状态0xC0E90002。已对照[微软NTSTATUS定义](https://github.com/microsoft/win32metadata/blob/main/generation/WinSDK/RecompiledIdlHeaders/shared/ntstatus.h)确认其为STATUS_SYSTEM_INTEGRITY_POLICY_VIOLATION，并用系统RtlNtStatusToDosError实际确认对应4551；不把通用标题误判成DLL格式损坏。上次诊断等待弹窗与该截图一致，但没有证据证明历史所有等待都只来自此弹窗。

load_check_library现在只在当前线程加载期间OR SEM_FAILCRITICALERRORS，保留其他位，finally恢复setter返回的旧模式；设置失败不加载，恢复失败在正常返回时导致失败，存在原加载异常时记录恢复错误并保留主异常。依据[微软SetThreadErrorMode文档](https://learn.microsoft.com/windows/win32/api/errhandlingapi/nf-errhandlingapi-setthreaderrormode)和[DLL加载弹窗说明](https://devblogs.microsoft.com/oldnewthing/20240208-00/?p=109374)，只调整错误呈现，不更改应用控制/签名信任。

本轮Python注入回归通过：旧模式0/2/1/3、成功/4551/126/KeyboardInterrupt均保留和恢复原模式；设置失败在CDLL之前退出；恢复失败不掩盖主异常。另对真实app_tasks_init.dll执行生产加载函数（测试AST抽取同一函数，未重复实现），20秒超时保护下自行返回BLOCKED/4551/退出1，无Traceback、不等待点击新弹窗，父线程模式不变。真实阻止仍在、没有C测试断言运行，不计入25组通过数；完整入口仍受构建工具和系统策略限制。

[GitHub工作流](../.github/workflows/software.yml)在push/PR执行原Windows检查、Ubuntu STM32 Debug/Release及官方ESP-IDF v6.1构建。不烧录、不部署；最终提交和CI结果以HANDOFF及实际运行记录为准。源码/文档已准备不等于已经提交/上传。

交付基线已本地提交022c551，分类移动被Git识别为重命名，原先未跟踪的源码/检查/文档均已纳入。推送的沙箱网络失败，随后沙箱外正常push自动审批连续两次超时，实际推送尚未启动；CI未运行。用户最新选择先保留本地提交，本轮停止推送；后续只有明确恢复上传才重新核对远程并推送。不关闭Windows保护、不force推送。

### 10-05及更早历史记录

用户确认相关外设未接、PA0未接后，按纯软件范围重新执行上述verify.ps1：20组生产逻辑电脑检查、STM32 Debug/Release及ESP-IDF6.1增量构建全部通过，入口返回0并声明未访问硬件。未修改业务代码、阈值或保护，不把0x5E清零作为软件验收条件。本次日志为software_checks/build/software_only_verification.log；未运行build目录的live、boot或SWD设备排查脚本。

随后按“代码运行是否报错、分类排序是否正确”的请求再次运行同一入口：20组全部通过，STM32 Debug/Release及ESP32增量构建成功，本轮日志无编译错误/警告，未访问设备。STM32增量为ninja no work to do，没有强制重新链接；不将此表述为再次全量编译或整个RTOS在电脑运行通过。输出为software_checks/build/code_and_layout_verification.log。

目录审计独立于20组C运行检查：CMake中27个STM32应用/自编驱动C文件、9个ESP32 C文件完整覆盖现有源码且无重复，include目录和所有相对源码include有效；45个迁移文件均位于新目录、旧位置不存在，未使用迁移前内容hash判断后续SDK修改。Services/main/检查根目录没有遗留平铺C/header副本，文档链接有效。职责分类与README一致，无需再次移动文件；CMake/文件排列不是任务运行顺序。结构审计日志为software_checks/build/layout_audit.log。

2026-10-05将ESP32构建基线改为官方v6.1，实际使用GCC 15.2.0、配套Python 3.11.2；旧生成配置已备份到本机忽略目录，清旧构建缓存后由sdkconfig.defaults重新生成。修正CMake最小版本/6.1.x约束，显式固定100 Hz tick，移除因Central关闭而不可见的GATT client显式设置。UART/NimBLE/FreeRTOS现有调用通过真实6.1头文件编译，不需要替换业务API；BLE停止worker仍保留，因为6.1的nimble_port_stop仍包含无限等待。

使用上面的verify.ps1完整入口：20组软件检查、STM32 Debug/Release、ESP32构建均通过。另从esp32目录，在5.4.4环境运行 `idf.py -B build/idf54_reject reconfigure`，确认在项目配置前被“requires ESP-IDF 6.1.x”拒绝；不是5.4.4兼容构建通过。此额外配置检查不计入20组C逻辑检查。

2026-10-04目录分类后，通过同一verify.ps1入口重新运行20组电脑检查、STM32 Debug/Release及ESP32构建，全部成功。32个移动的固件源文件和头文件当时内容校验值未变，测试相对include和编译源/include目录已调整；当时构建大小与前一轮一致，本次6.1的ESP32大小见下表。

2026-10-03完成五类功能源码检查，修复缓存传感采样时间刷新、UART服务状态读取不一致、CAN ISR缺少单次上限，以及CAN导出未主动清过期状态。新增UART/CAN生产驱动边界、Flash日志和ESP32 UART启动清理检查；20组全部通过，统一入口重新运行STM32 Debug/Release和ESP32构建。BLE/CAN非零运动仍拒绝，IWDG未启用。逐项检查、静态发现与限制见工程说明B8～B11及第9节。

最终统一入口返回0，并输出 `Software verification complete. No hardware was accessed.`
最新纯软件输出在本机 `software_checks/build/code_and_layout_verification.log`，结构审计为同目录layout_audit.log；此前纯软件检查为software_only_verification.log。本次增量构建没有重链接，以下产物大小仍取最近已记录的链接/镜像结果，不是重新测量。此前6.1迁移完整输出为同目录idf61_verification.log，旧SDK拒绝记录为idf61_version_guard.log；首次清缓存构建为idf61_initial_build.log。2026-10-04/03历史输出分别为reorganization_verification.log/verification.log。均是忽略的生成物，持久结果不依赖日志永久存在。

| 检查/构建 | 本次结果 |
|---|---|
| 主机检查 | 20组PASS，Clang `-Wall -Wextra -Werror` |
| STM32 Debug | FLASH 59024 B / 65536 B；RAM 17048 B / 20480 B |
| STM32 Release | FLASH 50592 B / 65536 B；RAM 17032 B / 20480 B |
| ESP32 ESP-IDF 6.1 | app 503888 B（0x7b050），1 MiB应用分区；bootloader 26176 B（0x6640） |

STM32本次没有源码修改，Debug/Release增量构建成功；FLASH沿用2026-10-04的链接器分配数字（含对齐），当前size工具核对data+bss分别为17048/17032 B。RAM数字为静态分配，包含静态任务栈/配置堆，不是运行期可用堆；编译器 `.su` 是单函数栈估算，不是任务栈峰值。

此前6.1配置构建没有项目C编译错误或编译器warning，但有5项IDF6.1内部CMake私有include依赖警告：esp_wifi的roaming_app/include、roaming_app/src、include/apps_private被wpa_supplicant使用，以及wpa_supplicant的src、esp_supplicant/src被esp_wifi使用；最新纯软件增量运行未再次出现这些配置警告，不代表已修复SDK依赖。未修改SDK源码、未关闭警告。首次GATT client不可见提示在删除无效defaults项后消失。未完成新旧NimBLE host栈/绑核配置的额外静态对照（读取未获批准），不能声称配置完全等价；现有100 Hz、单连接/绑定、隐私、SC与加密的生成配置已核对。设备NimBLE事件、NVS旧绑定升级/回退兼容、并发和栈水位仍待硬件验证；镜像大小变化不代表性能提升。

## 20组检查与证据边界

| 检查 | 覆盖 |
|---|---|
| UART原有自测 | ESP32协议长度、CRC、编码、恢复等，失败mask=0 |
| BLE原有自测 | 固定命令/状态数据格式、范围、语义、CRC，mask=0；不运行NimBLE |
| STM32/ESP32互通 | 实际MOTOR分片解析、PWM正负边界、ACK互通 |
| 电池状态机 | 启动资格、阈值、滞回、失效后重新资格、锁存 |
| CAN解码 | 有符号PWM、DLC、flags和范围 |
| 原ADC自测 | 生产换算+电池算法自测 |
| ADC边界 | 零输入、最大采样数、非法和/参考值/指针、失败清有效标记 |
| control_checks.c | 生产arbiter/RobotState/MotorTask单周期：STOP清旧命令、300 ms超时、CAN/UART序号回绕、UART与内部TEST所有权切换、提交失败、电池/供电门恢复、新命令要求、换向死区/20%重启；CAN非零拒绝不取owner/不入队、电池恢复仍拒绝、旧CAN STOP清UART目标 |
| link_checks.c | 生产ESP32握手策略：错误ACK、同tick/ACK后状态、500 ms新鲜、非零输出、1200 ms等待、重握手、回绕和长期在线 |
| wire_checks.c | 诊断固定字节向量、双端编码/解析/解码，坏revision/flags/长度；STM32状态到ESP解码再到BLE编码；STM32坏CRC/半帧超时/坏长度恢复 |
| communication_checks.c | 直接包含生产通信任务，测试先过期再接收、响应队列容量、实际诊断打包、状态导出时清过期数据 |
| CrossMcuScenario_Run | 连续执行ESP32编码→STM32生产通信处理→仲裁→电机周期→STATUS/ACK编码→ESP32解码/握手策略；覆盖丢ACK、队列满、坏CRC、诊断隔离、状态失效、恢复、序号和时间回绕、心跳不续期运动；追加SET_MODE往返及CAN/AUTO拒绝后零输出/无owner/ESP32保持在线 |
| BleLifecycleChecks_Run | 生产BLE生命周期决策函数：断连回调缺失后的重试期限、断连清除、毫秒计数回绕、换绑待处理时拒绝包括旧绑定在内的连接 |
| TaskHealthChecks_Run | 生产任务健康策略：不同期限、启动宽限、从未启动、锁存、32位tick及心跳计数回绕、Storage不因长擦写触发时间故障 |
| StartupChecks_Run | 编译生产`freertos.c`并替换内核状态、任务创建及错误处理边界，注入内核未Ready、默认任务与AppTasks初始化失败，验证进入Error_Handler；正常启动不误报。不是实际内存耗尽或调度测试 |
| AppTasksInitChecks_Run | 独立编译生产`app_tasks.c`/`task_health.c`，逐个注入两个队列、互斥锁、七个业务任务的创建失败；任一失败均返回false，全部成功才返回true。替身不调度任务，也不模拟内存碎片或ISR |
| UartDriverChecks_Run | 包含生产uart_driver.c，HAL寄存器/IRQ保护/通知替身：空闲、TX/完成、真正状态不一致、IDLE/TC、重复事件、DMA位置回绕、环溢出、错误与恢复失败/成功。检查Service状态读取的保护边界，不模拟真实抢占 |
| CanDriverChecks_Run | 包含生产can_driver.c和实际can_protocol.c：读取时补帧仍3帧返回、再次回调、STOP先取与控制覆盖、接收错误、50/51 ms发送超时、完成、bus-off与重建失败/成功。FIFO/HAL为替身，不验证重复IRQ下调度活性 |
| StorageLogChecks_Run | 包含生产storage_log.c，字节数组模拟NOR：记录字段/CRC与读回、初始化扫描、8 B断写、CRC损坏、读失败、擦除失败/未擦净、序号及512 KiB区域回绕、保留区未改。W25Q64 SPI驱动和真实掉电未执行 |
| EspUartStartChecks_Run | 包含完整生产robot_uart_link.c；逐个注入ACK队列、STATUS邮箱、UART安装/参数/引脚/上拉/flush、RX任务/link任务共9点失败，检查释放句柄、清资源、未提前通知、成功后重复启动拒绝及失败后可重试。创建替身不运行任务，测试才调用私有清理以重置模拟成功现场 |

控制测试的时钟、队列、调度器锁和电机I/O是测试替身；临界区为顺序执行的空边界，不能验证抢占、ISR、SMP、优先级反转或死锁。电机测试执行的是生产 `MotorTask_RunCycle`，不是复制算法；调度循环、物理GPIO/PWM、电源保护实际响应未执行。

2026-10-03扩展第8组，执行生产SensorMessage_SetSampleTime与RobotState失效判断，覆盖旧样本重发、0时间、200/201 ms、单/无有效字段与回绕；不执行SensorTask无限循环。第10组补共享失效策略→CAN编码，确认过期电压0xffff与错误位；不执行CanTask无限循环。

新增三项外设边界检查独立链接为drivers.dll，替身声明在driver_stubs/，只替换HAL、寄存器、事件及IRQ保护边界；保护深度计数不是NVIC模拟。ESP32启动检查另链接esp_uart_start.dll，SDK声明集中在esp_uart_start_stubs.h，run.py生成空include入口，真实SDK编译由ESP32固件构建验证。日志/SDK后台任务/队列竞争未在这些替身中运行。没有引入新依赖或复制生产解析、CRC、仲裁和启动算法。

通信测试直接include生产 `.c` 以访问内部调度器，不另造一套处理器；OS事件和UART读取替换为确定性输入。ESP32测试执行真实policy但不启动RX/link任务，所以任务间竞态和NimBLE安全生命周期仍需要运行环境验证。连续场景里非零MOTOR帧直接注入UART层，用来证明STM32内部路径与ESP32 STOP-only策略的组合行为；它不是BLE已开放DRIVE的证据。丢ACK和队列满通过测试边界注入，不是设备故障经历。

CAN场景直接调用生产 `ControlArbiter_SubmitCan`，不执行CanTask、CAN收发驱动或其调度。CAN codec另有生产解码检查，不能将这些检查合称物理CAN端到端验证。CAN V1无运动ACK；0x83反馈经实际Comm排队、UART编解码、新鲜度判断与BLE编码检查到达读取载荷，但测试没有执行真实ESP32 RX任务、GATT回调或手机读取。STATUS为零本身仍不能说明拒绝原因。

2026-10-02补CAN反馈时扩展第8/10/12组而未新增组（2026-10-03另新增第17～20组）：control_checks直接包含生产仲裁器以注入私有计数UINT32_MAX，验证自然回绕、结果分类、零时间和非法协议仅影响诊断；wire_checks验证0x83固定载荷字节向量、坏修订/flags/result/长度、BLE CRC与容量、3000/3001 ms新鲜度、离线/无样本/无CAN事件及age饱和；连续场景验证CAN安全拒绝经UART反馈进入BLE字段，诊断不推进STOP握手。反馈保存/新鲜度辅助函数是生产代码，真实portMUX竞争与缓存清除回调仍未运行。

Release本次 `.su`：CommunicationTask入口160 B、CanTask入口152 B，仍是单函数静态估算，不是栈调用链峰值或高水位；任务预算保留640/512 B。完整反馈字段、语义、取舍与限制见[工程说明](../docs/ENGINEERING.md)。

BLE新增检查执行 `robot_ble_lifecycle.c` 的生产判断，`robot_ble.c` 将其用于断连请求和连接授权。没有替换或运行NimBLE、事件组、SMP、Host reset回调和真实StatusTask调度；因此不能据此声称蓝牙断连或换绑已在设备上验证。

任务健康检查执行生产 `task_health.c`；MotorTask每周期采集，Comm诊断导出时再采集并把锁存掩码编码为0x82修订2。测试没有运行真实任务、Flash擦写或FreeRTOS锁；期限是根据任务周期与可见阻塞路径的静态预算，必须在设备上观察后校准。故障掩码仅作诊断，不直接喂IWDG或新增停机动作；现有命令超时与数据过期仍独立保护。修订1接收端不识别修订2诊断，双端须成对更新；STATUS和BLE格式未改。

启动检查通过`software_checks/startup_stubs/`替换CMSIS资源创建与错误处理调用，执行生产`MX_FREERTOS_Init`和独立DLL中的生产`AppTasks_Init`。业务资源现于调度器启动前创建，部分成功后失败不会让任务抢先运行；但替身没有真实调度，尚不能证明目标RTOS/硬件行为。`main.c`中调度器若返回会进入Error_Handler用户区；本测试没有执行HAL或真实内核。若STM32进入Error_Handler，ESP32只能从状态/诊断过期发现服务不可用，无法从链路得知失败原因。

## 失败到通过的记录

1. 旧RobotState实现对timestamp=0的有效传感数据不失效：新增control_checks最初第62行失败；改用有效位判断后通过。
2. 旧通信调度器在10 ms接收4 B、71 ms接收剩余6 B，错误接受过期STOP帧：新增communication_checks最初第36行失败；先处理旧缓冲后通过。
3. 2026-10-01旧CAN/AUTO仲裁接受400/300 PWM后，生产ESP32策略离线并要求STOP，随后UART STOP处理清零；连续场景确认这条软件链成立。改成“CAN返回SAFETY_BLOCKED且保持在线”的预期，旧代码在communication_checks.c当时第310行失败；修复Submit后通过。临时复现/失败日志分别为build/can_conflict_before.log、build/can_conflict_red.log，均为忽略生成物；当前回归和完整验证入口可重跑。详见工程说明B7。

BLE本轮另有两处静态发现：正常返回的 `ble_gap_terminate` 只在API报错时安排重试，若无断连回调就不会再检查；`OnReset` 曾清除尚未完成的物理换绑请求。均非旧版本电脑端复现或设备经历。修复后的期限与授权判断通过第13组检查，NimBLE实际事件时序仍待验证。

本轮静态发现STM32默认引导任务`osThreadNew`结果未检查：创建失败时业务任务不会启动，也不进入原有Error_Handler。已补齐检查；新增第15组注入任务失败、成功及内核未Ready分支。调度器意外返回也接入Error_Handler；其运行期行为未注入。

本轮进一步确认业务任务原本在调度器启动后逐个创建：后续资源失败前，已创建任务存在抢占运行窗口。已把生产`AppTasks_Init`移至调度器启动前，并用第16组注入十个创建点；这属于代码分析确认的风险与测试替身回归，不是设备上发生过的抢占故障。ESP32原有“BLE disabled”日志在Host清理失败时不成立，已改成“启动中止”，额外STOP通知失败也会打印；未运行真实NimBLE故障注入。

编号1～3是电脑端真实复现，不是在设备上遇到的bug。行号是当时输出，后续增加测试可能改变。其余已标明的BLE、启动、ESP握手边界及HAL忙等问题属于代码分析发现；注入的掉电/断线不是实机经历。

## 尚未验证

真实FreeRTOS并发、NVIC/DMA丢包和时序、NimBLE授权/换绑/退出清理及缺失回调的真实重试时序、RTOS诊断实际跨板传输、PVD/IWDG、ADC采样精度、I2C/SPI错误恢复、Flash掉电行为、车轮响应、任务栈运行水位、CPU负载和长期稳定性。

本轮源码检查覆盖这些路径，但PVD寄存器保护、ADC DMA超时/迟到回调、HC_SR04 TIM2/EXTI、MPU/OLED I2C与W25Q64 SPI事务尚无生产驱动注入检查。CAN ISR单次3帧不等于对持续IRQ洪泛的总体限流。I2C继承锁仍跨MPU稳定等待及OLED单页事务，不能宣称没有优先级反转影响或实时抖动。

本轮未启用硬件看门狗，未开放BLE/CAN非零运动；AUTO不是自主避障。加密CAN反馈特征的授权、读取、客户端服务缓存及实际跨板时序尚未验证。已实现/部分实现/未实现及看门狗后续方案见 [工程说明](../docs/ENGINEERING.md)。
