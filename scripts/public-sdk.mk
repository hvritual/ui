.PHONY: test-public-sdk test-public-sdk-runtime

test-public-sdk:
	python3 scripts/public_sdk.py check
	python3 scripts/public_sdk.py check --example custom-chart
	python3 scripts/public_sdk.py check --example trend-chart
	python3 -m unittest discover -s tests/sdk -p 'test_*.py' -v
	node tests/sdk/test_public_ui.cjs
	node tests/sdk/test_custom_ui.cjs
	node tests/sdk/test_trend_ui.cjs
	node tests/sdk/test_drawing_adapter.cjs
	mkdir -p out/public-sdk-contract
	$(CC) -std=c11 -Wall -Wextra -Werror -Wpedantic -O2 -I. tests/sdk/test_drawing.c -o out/public-sdk-contract/test-drawing
	out/public-sdk-contract/test-drawing

# Only after canonical Native/ARM Framework and licensed font assets exist.
test-public-sdk-runtime:
	python3 scripts/public_sdk.py integration \
	  --assets out/framework/applications/control-panel \
	  --native out/framework/native/ui-framework \
	  --arm out/framework/arm/ui-framework \
	  --output out/public-sdk/real-test
	python3 scripts/public_sdk.py integration --example custom-chart \
	  --assets out/framework/applications/control-panel \
	  --native out/framework/native/ui-framework \
	  --arm out/framework/arm/ui-framework \
	  --baseline-evidence out/public-sdk/real-test/evidence.json \
	  --output out/public-sdk/custom-chart
	python3 scripts/public_sdk.py integration --example trend-chart \
	  --assets out/framework/applications/control-panel \
	  --native out/framework/native/ui-framework \
	  --arm out/framework/arm/ui-framework \
	  --baseline-evidence out/public-sdk/real-test/evidence.json \
	  --output out/public-sdk/trend-chart
