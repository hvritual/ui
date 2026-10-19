.PHONY: test-chart-core test-chart-sdk test-chart-runtime

test-chart-core:
	mkdir -p out/chart/native out/chart/arm out/chart/sanitize
	gcc -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
	  hosts/linux/chart/draw.c tests/chart/test_draw.c -o out/chart/native/test-chart
	./out/chart/native/test-chart > out/chart/native/report.txt
	! ./out/chart/native/test-chart --intentional-failure > out/chart/native/negative.txt
	arm-linux-gnueabihf-gcc -static -mcpu=cortex-a7 -mfpu=neon-vfpv4 -mfloat-abi=hard \
	  -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic \
	  hosts/linux/chart/draw.c tests/chart/test_draw.c -o out/chart/arm/test-chart
	qemu-arm -cpu cortex-a7 out/chart/arm/test-chart > out/chart/arm/report.txt
	! qemu-arm -cpu cortex-a7 out/chart/arm/test-chart --intentional-failure > out/chart/arm/negative.txt
	cmp out/chart/native/report.txt out/chart/arm/report.txt
	gcc -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic \
	  -fsanitize=address,undefined -fno-omit-frame-pointer \
	  hosts/linux/chart/draw.c tests/chart/test_draw.c -o out/chart/sanitize/test-chart
	ASAN_OPTIONS=detect_leaks=1 ./out/chart/sanitize/test-chart > out/chart/sanitize/report.txt
	grep -q 'CHART_DRAW_OK four-series 1000 points' out/chart/sanitize/report.txt
	@echo CHART_CORE_NATIVE_ARM_ASAN_UBSAN_OK

test-chart-sdk:
	python3 scripts/chart_sdk.py check
	python3 -m unittest discover -s tests/chart -p 'test_chart_sdk.py' -v
	node tests/chart/test_charts.cjs

test-chart-runtime:
	python3 scripts/chart_sdk.py integration \
	  --assets out/framework/applications/control-panel \
	  --native out/framework/native/ui-framework \
	  --arm out/framework/arm/ui-framework \
	  --output out/chart/real-framework
