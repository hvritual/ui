# Engineering boundaries

- Implement one roadmap issue per task; use ordinary domain names in runtime code.
- Keep P0 toolchain smoke separate from P1 PocketJS UI runtime. No mock/stub may count as runtime acceptance.
- Never claim QEMU performance or generic GNU ABI as physical i.MX6UL evidence.
- Source revisions and all configuration changes require explicit diffs; do not silently update upstream.
- Preserve existing device control, network middleware and OTA services. No direct actuator access from UI.
- Keep both 1024x600 and 1024x800 profiles; unknown hardware fields remain null until measured.
- Run the issue's gates. Missing tools, failed tests, missing artifacts and stale evidence must fail closed.
- Follow an ordinary branch / PR workflow; do not force-push or bypass repository protection.
- Source, build logs, ELF ABI, raw test output and hashes must identify the tested commit.
