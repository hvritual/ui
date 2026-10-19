.PHONY: test-custom-surface test-custom-surface-runtime

test-custom-surface:
	python3 scripts/custom_sdk.py check
	python3 -m unittest discover -s tests/sdk -p 'test_custom_sdk.py' -v
	node tests/sdk/test_custom.cjs

# Requires exact-head canonical real Native/ARM Framework builds and licensed assets.
test-custom-surface-runtime:
	python3 scripts/custom_sdk.py integration \
	  --assets out/framework/applications/control-panel \
	  --native out/framework/native/ui-framework \
	  --arm out/framework/arm/ui-framework \
	  --output out/public-custom/real-test
