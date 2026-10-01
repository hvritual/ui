.PHONY: test-r6-board-verifier verify-r6-board

test-r6-board-verifier:
	python3 scripts/r6_hil.py --self-test

verify-r6-board:
	@test -n "$(REPORT)" || (echo "REPORT=/path/to/returned-r6-evidence is required" >&2; exit 2)
	@test -n "$(MANIFEST)" || (echo "MANIFEST=/path/to/coffee-framework/manifest.json is required" >&2; exit 2)
	python3 scripts/r6_hil.py --report "$(REPORT)" --manifest "$(MANIFEST)"
