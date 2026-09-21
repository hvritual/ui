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
