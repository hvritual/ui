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
