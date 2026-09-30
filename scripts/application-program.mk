.PHONY: test-application-program test-application-program-arm test-application-program-sanitize verify-application-program
test-application-program:
	python3 scripts/application_program.py test --mode native
test-application-program-arm:
	python3 scripts/application_program.py test --mode arm
test-application-program-sanitize:
	ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 python3 scripts/application_program.py test --mode sanitize
verify-application-program:
	python3 scripts/application_program.py verify
