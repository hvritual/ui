.PHONY: check fetch build smoke verify
check:
	python3 scripts/port.py check
fetch:
	python3 scripts/port.py fetch
build:
	python3 scripts/port.py build
smoke:
	python3 scripts/port.py smoke
verify:
	python3 scripts/port.py verify

.PHONY: check-targets fetch-runtime build-runtime build-runtime-arm test-runtime test-runtime-arm verify-runtime
check-targets:
	python3 scripts/runtime.py check
fetch-runtime:
	python3 scripts/runtime.py fetch
build-runtime:
	python3 scripts/runtime.py build native
build-runtime-arm:
	python3 scripts/runtime.py build arm
test-runtime: build-runtime
	python3 scripts/runtime.py test native
test-runtime-arm: build-runtime-arm
	python3 scripts/runtime.py test arm
verify-runtime:
	python3 scripts/runtime.py verify

.PHONY: test-display-unit test-display test-display-arm verify-display
test-display-unit:
	python3 scripts/display.py unit
test-display: test-runtime
	python3 scripts/display.py test native
test-display-arm: test-runtime-arm
	python3 scripts/display.py test arm
verify-display:
	python3 scripts/display.py verify

.PHONY: build-device-package
build-device-package: test-display-arm
	python3 scripts/device_package.py

.PHONY: test-vsync-unit test-vsync test-vsync-arm verify-vsync
test-vsync-unit:
	python3 scripts/vsync.py unit
test-vsync: test-runtime
	python3 scripts/vsync.py test native
test-vsync-arm: test-runtime-arm
	python3 scripts/vsync.py test arm
verify-vsync:
	python3 scripts/vsync.py verify

.PHONY: test-input-unit test-input test-input-arm package-input verify-input
test-input-unit:
	python3 scripts/input.py unit
test-input:
	python3 scripts/input.py test native
test-input-arm:
	python3 scripts/input.py test arm
package-input:
	python3 scripts/input.py package
verify-input:
	python3 scripts/input.py verify
