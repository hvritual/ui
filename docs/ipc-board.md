# IPC 上板测试包

本包用于在目标板上运行真实的 Unix socket、独立 broker、后台服务和当前
Application VM／renderer，检查 Query、Command、Event、State 四类通信。
它使用合成计数器业务，不调用机器控制接口。

**当前 IPC 入口使用 headless 模式：不打开 framebuffer 或触摸设备。**
测试仍会运行 UI 应用和渲染器，并保存 1024×600、1024×800 的实际渲染帧。
在同一块板上执行两个逻辑分辨率，不代表两种物理屏幕已经验收。
需要 LCD／触摸测试时，使用另行提供的 `imx6ul-coffee-framework.tar.gz`
及其 `run-framework.sh`；它的界面不是本包的 IPC 计数器界面。

## 包内容与适用条件

`imx6ul-ipc-headless.tar.gz` 解包后为 `pocket-ipc-board`，包括：

| 文件 | 用途 |
|---|---|
| `ui-framework` | 当前生产 Framework ELF；应用业务由 `.pui` 加载 |
| `pocket-ipcd` | 通用路由、动态成员注册、请求和订阅管理 |
| `service-fixture` | 独立合成计数器服务，可注册不同服务名和四类成员名 |
| `packages/*.pui` | 2 个业务包、4 个缺权限包和 1 个离线探针包 |
| `run-ipc.sh` | 板端启动、执行、超时处理、子进程关闭和证据采集 |
| `verify-ipc-evidence.py` | 在开发电脑上校验回传证据，只需 Python 3.10+ 标准库 |
| `manifest.json`、`SHA256SUMS` | 提交、文件身份和完整性校验 |
| 构建、归属与 ABI 记录 | 交付 ELF 的来源、编译输入和静态链接证据 |

三个 ELF 都使用仓库锁定的工具链，按 ARMv7／Cortex-A7、NEON/VFPv4、
hard-float 构建并静态链接。构建门禁逐一检查无 `INTERP`、无 `NEEDED`，
所以不要求板上存在 `/lib/ld-linux-armhf.so.3` 或匹配的 GNU 动态 libc。
这仍不等于实际 BSP／内核兼容已经验证；最终以板上真实执行和回传记录为准。

板端需要 ARM Linux、可执行的测试目录、可写的 `/tmp`，以及提供标准命令的
POSIX shell／BusyBox，包括 `sha256sum`、`mktemp`、`sed`、`grep`、`find`、
`date`、`uname`、`id`、`sleep`、`cat`、`cp`、`dirname`、`chmod`、`mkdir`、
`rmdir` 和 `rm`。
板端不需要 Python、Node、jq、QEMU 或开发工具链。
同一用户运行脚本及三个进程；broker 使用实际 UID 校验连接身份。

包中 `.pui` 使用显式开发加载权限，未签名发布者认证仍为 false。
命令幂等记录仅保存在测试服务内存中，不提供断电持久化保证。
测试包不安装开机服务，不修改系统库、内核或现有设备控制／网络／OTA 服务。

## 在板上执行

将压缩包复制到可写且允许执行程序的测试目录，完整解包到新目录。
不要把不同版本的 ELF、`.pui` 或脚本混在一起。

```sh
tar -xzf imx6ul-ipc-headless.tar.gz
cd pocket-ipc-board
sha256sum -c SHA256SUMS
./run-ipc.sh
```

脚本默认执行全部 9 个场景。它不使用 LCD／触摸，因而无需为了本包停止原有
显示进程；实际测试负载对机器性能的影响应由现场测试环境决定。
输出默认保存在包目录下新建的 `logs/ipc-时间-PID` 中。也可以指定一个
**尚不存在的绝对路径**，其父目录必须已存在：

```sh
./run-ipc.sh --output /mnt/test-results/ipc-run-001
```

9 个场景的前后 PPM 帧共有 14 张 1024×600 和 4 张 1024×800，纯像素合计
35,635,200 字节，约 34 MiB；还需为包、报告和日志留出空间。结果应写到
容量足够的存储位置，短 socket 目录才放在 `/tmp`。这个文件体积不是运行
内存占用或实板剩余 RAM 的测量值。

正常业务场景各观察 8 秒，权限／离线场景各观察 4 秒。服务启动等待上限为
15 秒，单个 UI 子进程运行硬上限为 90 秒。业务请求自己的超时约定保持不变；
测试失败时应保留记录，不能通过修改报告或无限重试把失败转换为通过。

| 场景 | 预期结果 |
|---|---|
| `example-600`、`example-800` | 四类通信、同键命令去重与冲突、Event 过滤、State 进入／移除完成；最终计数为 2 |
| `vendor-600`、`vendor-800` | 在原 broker 仍运行时加入新的服务名和四类成员名，复用同一个 UI 和 broker ELF 完成同样业务 |
| `denied-query` | 缺 Query 权限，返回 `UNAUTHORIZED`，不发送业务请求 |
| `denied-command` | 缺 Command 权限，返回 `UNAUTHORIZED`，不执行变更 |
| `denied-events` | 缺 Event 权限，订阅以 `UNAUTHORIZED` 结束 |
| `denied-state` | 缺 State 权限，watch 以 `UNAUTHORIZED` 结束 |
| `offline` | broker 不存在，返回 `UNAVAILABLE`；VM 保持健康并正常清理 |

四个拒绝场景和离线场景在 600 的逻辑分辨率执行。权限错误和离线错误是
这些场景的预期结果；不能将它们误认为“所有调用都应该成功”。

脚本结束会打印 `LOG_DIR=...`。`IPC_BOARD_COLLECTED mode=board` 仅表示本次
直接执行和采集已完成；还应运行下述宿主校验器。失败返回非零状态并保留
失败记录。退出码 130 表示 Ctrl-C，143 表示收到 TERM。

## 停止与重复运行

在执行终端按 Ctrl-C，或向该脚本进程发送 TERM。脚本只清理自己启动的
UI、provider 和 broker 子进程，先等待正常退出，必要时在有界等待后结束
这些子进程，并将异常退出记为失败。它不执行 `killall`，不重启其他服务。

每次运行创建独立的 `/tmp/pocket-ipc.XXXXXX` 私有目录，socket 不复用。
正常结束由 broker 和脚本清理本次目录。SIGKILL 或掉电无法执行 shell trap；
遇到这种情况应先检查原进程及日志，再由现场操作员处理遗留进程／目录。
下一次运行仍使用新目录，不把已有 socket 当作本次 broker。

新测试使用新证据目录；旧报告不会被覆盖。完成测试后可移除本次测试包及
已备份的结果目录，不需要卸载系统组件或恢复系统配置。

## 回传与校验

回传脚本打印的完整 `LOG_DIR`，不要只发送 `report.json` 或最后一行日志。
其中包含 `run.json`、包 manifest 副本、环境记录、进程 PID／退出记录、
9 个 UI 日志、各场景的 `report.json`、`timeline.csv`、`first.ppm`、
`last.ppm` 和 `SHA256SUMS`。可在板上用 `tar` 压缩整个目录后带回开发电脑。

在开发电脑上解包**同一版本**的测试包，将回传目录放在另一个路径：

```sh
cd pocket-ipc-board
python3 verify-ipc-evidence.py --package . --report /path/to/returned/ipc-run-001
```

校验覆盖包／ELF／`.pui` 身份、完整场景矩阵、UI 业务断言、预期权限错误、
无待处理请求、受监测的 core allocator 关闭后无活跃分配、渲染尺寸及像素
变化、子进程记录和文件哈希。
缺失、篡改、混版本、错误执行模式或未完成的结果均应失败。
校验器不替代实物身份和操作者复核，也不验证实际 LCD、触摸或机器执行器。

请同时注明板型号、实际连接方式、运行环境和异常现象。未知硬件字段保持未知，
不要把 600 板的 libc、轴范围、触摸协议或屏幕测量值复制给 800 板。
各 UI report 的 `physical_io`、`visual_validated`、`business_commands` 和
`package_authenticated` 保持 false，这是本次诊断的正确范围。

## 从源码重建及软件证据

源码必须是仓库锁定工具链下的干净已提交状态，先完成现有 Runtime、Framework
及两架构 IPC 集成门禁，再执行：

```sh
make build-board-framework test-board-framework-package
make build-board-ipc test-board-ipc-package
```

完整前置步骤由 `.github/workflows/coffee.yml` 固定。
输出为 `out/ipc-board/imx6ul-ipc-headless.tar.gz`，独立 CI artifact 同时保存
最终压缩包 SHA、原始软件证明、构建记录和该提交源码归档。

构建器对最终压缩包安全解包，再运行其中的同一个 `run-ipc.sh`，显式传入
`--emulator /absolute/path/to/qemu-arm`。这种执行记录的模式为 `qemu`，不能
作为上板记录。普通板端执行无需此选项。测试后不会修改已测压缩包或重标
其中的提交；包内 manifest 是该包的版本依据。

QEMU 的真实独立进程测试证明软件链路与交付文件一致，不证明实际板端时延、
内存余量、BSP 兼容、LCD／触摸验收或生产认证。#8、#9、#49、#7 的相应
验收继续独立进行。
