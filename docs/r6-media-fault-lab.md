# R6 / #80：动态资源 A → B → 回滚、忙时延迟及受控触摸恢复

本独立任务属于 #49（R6 实机功能收口），承接已完成 #78 与 #79。
**本工具只负责已授权的签名测试资源操作、操作时序记录与只读核验，不会连接远端设备、重置触摸驱动、伪造 SYN_DROPPED 或替人工批准。**

## 固定基线与隔离范围

继续使用已在 600 板完成中文输入的完整 `coffee-framework` 板端包，不为新工具修改运行时：

- 构建提交 `24a6f836f663e0128f5c7c7bb204c25a4caff68e`
- main 合并提交 `b4b683c39516f1baffb922aa180cefd5fab2d484`（与构建提交源码树一致）
- 原设备包 SHA256 `dd52883a145b5501b4bf150da20c19865f7ebe14816176d367a20fd7d69d1d72`
- 600 板在用户现场确认简体中文输入可正常完成；其原始成功场次和具名人审仍待保存

在闲置、隔离、不对顾客提供服务的设备上运行。现场依据既有维护流程仅退出竞争的显示/输入应用，保留设备控制、网络和 OTA，保留恢复原 UI 的操作方式。**本仓库没有远程设备控制权限，也不会调用任何 sysfs 解绑/重绑、输入节点写入或重启命令。**

`r6-media-fault-lab.sh` 是额外的现场辅助脚本，可经受控渠道复制到测试板的**原始设备包目录之外**；它不改变原 tar、`ui-framework`、签名公共密钥或 `SHA256SUMS`。它需要板上现有 `sh`、`sha256sum`、`awk`、`date`、`sed`、`sleep`、`mediactl`，**不需要 Python**。实际原始日志仍由 `run-framework.sh` 生成。

## 第一阶段：单一 Coffee 场次的图片更新与忙态验证

创建独立会话和新的本地媒体存储；原来的 `coffee-framework/media-store` 不会被写入。在板端使用两个终端：

```sh
# 终端 B：复制辅助脚本到设备包之外的批准目录，例如 /tmp/r6-lab-tools/
# BUNDLE 代表已完整解包并通过校验的原始 coffee-framework 目录
BUNDLE=/approved/coffee-framework
mkdir -p /tmp/r6-field-sessions
sh /tmp/r6-lab-tools/r6-media-fault-lab.sh prepare \
  "$BUNDLE" /tmp/r6-field-sessions imx6ul-1024x600
```

命令输出唯一的 `R6_SESSION_DIR` 和 `R6_MEDIA_STORE`。例如用实际输出设置：

```sh
SESSION=/tmp/r6-field-sessions/r6-mf-<本次输出的目录名>

# 终端 A：运行原始物理 Coffee，既有界面进程需先按现场批准方法正常退出
FRAMEWORK_APPLICATION=coffee FRAMEWORK_ITEMS=100 \
FRAMEWORK_STORE_DIR="$SESSION/store" \
  "$BUNDLE/run-framework.sh" imx6ul-1024x600 900

# 终端 B：确认 A、B、回滚过程，所有 YES 仅代表操作员观察，不是程序自动认证
sh /tmp/r6-lab-tools/r6-media-fault-lab.sh media "$BUNDLE" "$SESSION"
```

交互助手依次操作：

1. 实际空闲首页时，使用包内 `mediactl install` + `updates/demo.public` 装载签名 A。确认 LCD 确实显示 A。
2. **先进入真实 Detail/Modal 或保持触摸按下状态**，再安装签名 B；保持忙至少 3 秒，目视确认仍为 A。
3. 返回空闲 Home，目视确认 B 出现，检查控件没有不可预期响应。
4. 再进入忙态，执行 `mediactl rollback` 请求回退 A；忙时应仍显示 B；回 Home 后应显示原 A。
5. 保留实际 LCD／手指视频、助手的 `provenance.tsv`、`actions.tsv`、`install-a.out`、`install-b.out`、`rollback.out` 及对应 status 收据；**原始 `LOG_DIR` 整目录保留**。

助手只调用包内真实签名安装器及查询命令。`MEDIA_INSTALLED requested_generation` 和 `mediactl status` **只是请求，不是实际生效**；后续只读审阅会检查该**同一场次** `startup.log` 中三次 `MEDIA_APPLIED generation=A,B,A`，以及最终 `report.json` 中 `media_applied >= 3`、`media_deferred >= 1`。如果缺少相应记录，必须保留失败／待验结果，不能修改计数、旧资源或图片内容。

> 已在隔离的软件环境用上一个确切板端包的**原始静态 ARM `mediactl`**、锁定版 Cortex-A7 QEMU 运行签名 A→B→回滚 A，三个 requested generation 一致、A/B 不同；这验证**安装器与存储**，不证明实机 LCD、忙时延迟或触摸恢复。

## 第二阶段：经过现场批准的 disconnect/reconnect、SYN_DROPPED

**只有当现场已有明确批准的设备、场地、SOP、负责人员及恢复途径时**，才进入此阶段。脚本不提供解绑 I2C/USB 输入驱动的猜测命令、停止系统服务的自动化或用户态“写出一个 SYN_DROPPED”的假测试。EVDEV `SYN_DROPPED` 是**读取方溢出/失步**的内核事件，普通点击、打开/关闭屏幕键盘、修改任意 JSON 字段都不能替代。

在同一段真实 Coffee 物理运行期间，从第二终端执行：

```sh
R6_APPROVED_FAULT_SOP=现场已批准的SOP编号 \
sh /tmp/r6-lab-tools/r6-media-fault-lab.sh fault "$BUNDLE" "$SESSION"
```

按批准的**外部隔离实验方法**分别进行触摸设备断开→重新出现→重新准入，以及令当前真实 evdev reader 产生 `SYN_DROPPED` 的实验。辅助脚本仅记录操作员确认的时间窗口，不能操作驱动。每次恢复后复测正确导航、弹窗背景无穿透、无幽灵点击、无老焦点/捕获事件，拍摄 LCD／手指与真实原始内核/应用日志。

如果现场没有**可执行且经过批准**的产生真实 `SYN_DROPPED` 的方法，保持这一项 `pending`；不能擅自让测试工具写 `/dev/input`、`/sys`、重载驱动或故意影响正在提供服务的机器。

## 只读、不会盖章的证据校验

在有 Python 3.10+ 的评审电脑/云端（不用在设备安装 Python）放置原始文件：

- 完整且原封不动的正式 `coffee-framework` 板端包：`BUNDLE`。
- 现场辅助脚本的完整会话目录：`SESSION`，包含 `actions.tsv` 和签名请求回执。
- 终端 A 打印的原始 `LOG_DIR`：`RUN`，包括 `startup.log`、`runtime/report.json`、`runtime/input.json`、`runtime/display.json`、截图、时间线和 `SHA256SUMS`。

然后运行：

```sh
python3 scripts/r6_media_fault_review.py \
  --bundle /review/coffee-framework \
  --session /review/r6-mf-xxxx \
  --run /review/original-LOG_DIR \
  --output /review/output/outside-all-inputs/r6-media-fault-review.json
```

检查器拒绝版本混用、源码/程序包不一致、原始日志哈希改变、合成回放冒充实机、签名安装收据错乱、A/B/回滚错序、没有 `MEDIA_APPLIED`、没有 `media_deferred` 或缺少任意真实恢复计数。工具不复制用户的输入内容和候选文字；它的 `board_accepted`、`product_admitted` **始终是 `false`**，`human_review_required` 始终为真。

| 退出码 | 含义 |
|---|---|
| 0 | 媒体与恢复场次的原始资料**形式一致**；仍必须人工核对真实 LCD/现场实验，不等于 R6 整体验收 |
| 1 | 证据不一致或系统计数与操作收据相矛盾，必须调查 |
| 2 | 运行未结束、收据或对应操作尚未提供，保留待验 |

该审阅器**不替换** #79 的分批预检或原始 `scripts/r6_hil.py` 最终四场次门禁。实际完整关闭仍要求 600/800 两板 × Coffee/Input 原始报告、触摸方向、资源与异常恢复、具名人审和相应 LCD 视频；场次之间不得累计拼接计数。P5 性能及量产认证另行验收。

## 适用文件与测试

- `scripts/device/r6-media-fault-lab.sh`：板端额外操作助手，不写驱动、不修改 Runtime。
- `scripts/r6_media_fault_review.py`：评审端原始元数据的只读比对。
- `tests/r6/test_media_fault_review.py`：合成证据的正负测试，只能验证门禁，不是板端实验。
- `.github/workflows/r6-media-fault.yml`：同提交软件测试与源码身份、工具哈希锁定。
