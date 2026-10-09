# R6 / #76: offline Pinyin physical recovery

Scope: Development-only i.MX6UL diagnosis. No publisher authentication, market release or physical acceptance is implied.

## Root-cause boundary
Real board: keyboard opens but selecting Simplified Chinese and offline Pinyin fail. Previous provider collapsed Linux errors into POCKET_PINYIN_UNAVAILABLE. Linux 4.9 alone does not establish whether this BSP supports sealed memfd, and it does not prove that the fallback is the root-cause fix.

#77 prefers sealed memfd and permits owner-only unlinked read-only temporary dictionary files only for ENOSYS, EINVAL or EOPNOTSUPP on memfd/seals. EPERM and unexpected resource/security failures are denied. The fallback reopens the same inode read-only, requires successful unlink, validates a pinned SHA256 and passes only a read-only FD to the owned decoding worker. User input, preedit, predictions and personal history are never persisted.

## Exact-version board procedure
Obtain the complete approved Framework artifact built from the PR's tested head commit, and verify the artifact SHA256. Do not mix Runtime, fonts, applications, dictionary and manifest across builds; do not relabel an artifact as the later merge commit. Operate only on an idle isolated lab board in a new directory. Stop only competing display/input UI consumers; preserve device control, network middleware, OTA and the normal UI recovery procedure.

Inside the extracted coffee-framework directory:

    sha256sum -c SHA256SUMS
    ./run-input.sh imx6ul-1024x600 180

Note: run-framework.sh without the coffee-ime selection launches the legacy Coffee application and does not enable offline Pinyin. run-input.sh explicitly selects the coffee-ime.pui unsigned DEVELOPMENT diagnostic; it is not authenticated production deployment. The 800 board needs its own verified touch name and orientation parameters, never borrowed from the 600 board.

## Short physical closure route
1. Open Text input and focus the ordinary name field, not a PIN/password.
2. Open input-language chooser and choose Simplified Chinese; verify the Pinyin indicator.
3. With synthetic text only, input nihao and observe real Han candidates.
4. Select 你好, verify committed/editable Han text, confirm and close editor.
5. Switch English; check normal editing, deletion, focus and reopen.
6. Verify Password/PIN suppress composition, language chooser and prediction; never enter real credentials.
7. Preserve the entire printed LOG_DIR and actual LCD/finger video, without rewriting original evidence.

## New report.json fields (metadata only)

| Field | Meaning |
|---|---|
| ime_provider_status | 0 OK; 1 pending; 2 invalid; 3 unavailable; 4 timeout; 5 exhausted |
| ime_provider_stage | 0 none; 1 dictionary; 2 memfd; 3 tempfile; 4 seal; 5 verify; 6 socket; 7 fd enumeration; 8 fork; 9 decoder |
| ime_provider_errno | Linux errno when applicable; 0 for non-syscall errors |
| ime_provider_storage | 0 none; 1 sealed memfd; 2 unlinked read-only fallback |
| ime_input_locale | 1 English; 2 Simplified Chinese while editing |
| ime_candidate_batches | Nonempty completed real candidate batches |
| ime_commits | Actual IME commits |

If provider fields remain zero and no candidates appear, investigate package admission and touch/focus dispatch rather than assuming memfd failure. A decoder failure may set stage=9 without a syscall errno. This report is final metadata, not continuous tracing or proof of actual LCD interaction. Never log or return user text, its hashes, preedit or candidate content.

The software PR is accepted only after exact-head CI and reviewed artifacts. Issue #76 remains open until a real 600-board run confirms switching, candidate generation and committed Han text; R6 #49 retains the separate 600/800 Coffee+IME, media, fault and human-review gates. QEMU is not a physical proof.