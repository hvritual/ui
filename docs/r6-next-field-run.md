# R6 下一步：保留已恢复版本，分批收口实机功能

归属 #49。此任务不更换 Runtime，不重做输入法，不进入 P5 性能优化或 Engine 迁移。

## 当前基线与确认边界

在 #78 上板包交付后的会话中，用户确认：“已经可以正常完成中文输入”。这记为现场人员文字确认的功能恢复，不伪造成功 report.json、设备 ID、输入次数或视频，也不把旧版失败报告改成成功。#76 不再阻塞下一项准备；按其原关闭条件，仍需归档成功运行的原始日志与版本绑定。

继续使用已经交付的完整包，不为本次工具更新重新上板：

- 构建提交：`24a6f836f663e0128f5c7c7bb204c25a4caff68e`
- 合并提交：`b4b683c39516f1baffb922aa180cefd5fab2d484`
- 设备归档 SHA256：`dd52883a145b5501b4bf150da20c19865f7ebe14816176d367a20fd7d69d1d72`
- 原交付文件：`imx6ul-framework-R6-mmap-eperm-24a6f836.tar.gz`

后续主线有纯工具提交时，不改写该包 manifest 的构建提交。不同包/不同会话不能拼接累计计数来满足一个最终验收场次。

## 现场执行顺序

### 1. 保存已成功的中文输入场次

保留上次成功运行完整 LOG_DIR，不必为了证明同一个正常行为先重复测试。若已有场次未完成取消、重开、密码/PIN、候选翻页或安全关闭，则这些仍是输入场次的待补项；预检会区分“已收到”、“未覆盖”和“不一致”。用户文字确认不是完整四场次人审。

### 2. 600 Coffee 完整功能与资源/恢复

仅在闲置隔离测试机上操作；按已批准的现场流程停止竞争的显示/输入消费者。不得停止设备控制、网络或 OTA，不覆盖正常 UI，不自动 reset 驱动。

在既有完整 coffee-framework 目录：

```sh
sha256sum -c SHA256SUMS
FRAMEWORK_APPLICATION=coffee FRAMEWORK_ITEMS=100 ./run-framework.sh imx6ul-1024x600 600
```

600 秒是可调整的本地采集时长，不是验收完成承诺。先完成 Home→Detail→确认弹层→模拟 Making→Success→Home，再检查遮罩防穿透、跨卡片拖动/取消、100 条虚拟列表、主题和显示语言。

动态图片 A→B→回滚及忙时延迟，继续使用已有媒体安装/回滚工具与获准资源；本任务不新建签名密钥、不伪造更新计数。断开/重连和 SYN_DROPPED 必须使用经现场批准的隔离实验方法，本工具不会自行注入故障。缺实验方法或资源时先回传已有日志，不临时操作客户设备，也不把普通点击当故障测试。

完整 Coffee 关闭门仍要求该场次的 media_applied、disconnects、reconnects、syn_dropped 等满足原 `r6_hil.py` 条件。另行场次可做排查，但不合成一份“通过”报告。

### 3. 600 Input 补足尚未覆盖行为

沿用上次已成功场次，按缺项补测。需要新场次时执行：

```sh
./run-input.sh imx6ul-1024x600 300
```

普通名称字段：中英文切换、真实候选/翻页/提交、编辑删除、确认一次、再次打开后取消一次；Password/PIN 保持无组合、无候选、无学习和语言弹窗；只用合成数据。结束前关闭编辑器，保留安全最终画面，不关闭截图隐私保护。输入成功不证明这些额外操作都已经完成。

### 4. 800 独立准入，然后重复 Coffee/Input

800 的控制器名称、轴范围、swap/invert 必须来自该板测量；没有这些信息时保持待验，不能填 600 参数或默认全零。

```sh
# 先按实际测量设置下面四个环境变量，不使用猜测值。
: "${TOUCH_NAME:?需要800板实测控制器名称}"
: "${SWAP_XY:?需要800板实测方向}"
: "${INVERT_X:?需要800板实测方向}"
: "${INVERT_Y:?需要800板实测方向}"
FRAMEWORK_APPLICATION=coffee FRAMEWORK_ITEMS=100 ./run-framework.sh imx6ul-1024x800 600 \
  --touch-name "$TOUCH_NAME" --swap-xy "$SWAP_XY" --invert-x "$INVERT_X" --invert-y "$INVERT_Y"
./run-input.sh imx6ul-1024x800 300 \
  --touch-name "$TOUCH_NAME" --swap-xy "$SWAP_XY" --invert-x "$INVERT_X" --invert-y "$INVERT_Y"
```

## 分批回传与只读预检

在有 Python 3.10+ 的评审电脑/云端操作，不要求设备安装 Python。可以先只回传一个场次。按原有 R6 结构复制完整 LOG_DIR，保留源文件内容；不要手工删改 JSON、CSV、PPM、SHA256SUMS。视频由现场提供并绑定，review.json 只由审核人员填写。

```text
returned-r6/
  imx6ul-1024x600/
    coffee/  # 完整 Coffee LOG_DIR 原文件
    input/   # 完整 Input LOG_DIR 原文件
    review.json  # 尚无人审就留缺失，不生成已批准模板
  imx6ul-1024x800/
    coffee/
    input/
    review.json
```

新工具复用原来的 `r6_hil.py`，不引入另一份验收阈值，也不修改原门禁。它逐项报告文件、哈希、程序/包身份、显示、触摸、时间线、Coffee/IME 操作和人审缺项；仅输出白名单计数，不复制用户输入正文。

```sh
python3 scripts/r6_intake.py \
  --report /path/to/returned-r6 \
  --manifest /path/to/complete-coffee-framework/manifest.json \
  --output /path/outside-evidence-and-bundle/intake.json
```

输出文件必须是原始证据和设备包目录之外的新文件，不能覆盖旧文件。省略 --output 时输出到终端。

| 退出码 | 含义 |
|---|---|
| 2 | 尚缺文件、场次或人工审核，继续收集 |
| 1 | 某项校验拒绝：例如版本不符、篡改、回放冒充实机或操作未覆盖；查看每项原因 |
| 0 | 四场次与人工审核通过原校验器的一致性检查，**不等于工具授予实机/产品准入** |

工具始终输出 `board_accepted=false` 与 `product_admitted=false`。一致性检查不能证明设备与视频的真实性；具名人审仍必须独立完成。完整验收继续使用原命令：

```sh
make -f scripts/r6.mk verify-r6-board \
  REPORT=/path/to/returned-r6 \
  MANIFEST=/path/to/complete-coffee-framework/manifest.json
```

## 软件自检

```sh
python3 -m unittest discover -s tests/r6 -p 'test_*.py' -v
make -f scripts/r6.mk test-r6-board-verifier
```

测试夹具只是工具单元测试，不能作为任何设备回传。只有 #49 的双机型功能门收口后，才进入独立 Engine 决策和正式性能阶段；本任务不自动放行 R7、P5、P6 或量产。
