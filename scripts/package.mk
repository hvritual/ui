.PHONY: test-package-contract test-package-contract-arm test-package-contract-sanitize
# Run from the repository root. Fresh output directories preserve prior evidence.
test-package-contract:
	python3 tests/package/test_package.py --output out/package/native-$$(date +%s%N)
test-package-contract-arm:
	python3 tests/package/test_package.py --cc arm-linux-gnueabihf-gcc --runner "qemu-arm -cpu cortex-a7" --output out/package/arm-$$(date +%s%N)
test-package-contract-sanitize:
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 tests/package/test_package.py --sanitize --output out/package/sanitize-$$(date +%s%N)
