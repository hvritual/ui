# Run from the repository root. These gates do not change the Runtime build.
.PHONY: check-language-admission test-language-admission verify-language-admission
check-language-admission:
	python3 scripts/language_admission.py check
test-language-admission:
	python3 -m unittest discover -s tests/language -v
verify-language-admission:
	python3 scripts/language_admission.py current --framework out/framework --output out/framework/language-admission
