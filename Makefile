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
	python3 scripts/seal_device_package.py

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

.PHONY: test-input-live-unit test-input-live test-input-live-arm verify-input-live
test-input-live-unit: test-runtime
	python3 scripts/input_live.py unit
test-input-live: test-runtime
	python3 scripts/input_live.py test native
test-input-live-arm: test-runtime-arm
	python3 scripts/input_live.py test arm
verify-input-live:
	python3 scripts/input_live.py verify

.PHONY: build-demo test-assets test-demo-native test-demo-arm verify-demo package-demo
build-demo:
	python3 scripts/coffee_assets.py
test-assets:
	python3 scripts/coffee.py media-test
test-demo-native: test-runtime
	python3 scripts/coffee.py test native
test-demo-arm: test-runtime-arm
	python3 scripts/coffee.py test arm
verify-demo:
	python3 scripts/coffee.py verify
package-demo:
	python3 scripts/coffee.py package

.PHONY: build-scene test-scene-native test-scene-arm verify-scene package-scene
build-scene:
	python3 scripts/scene.py build
test-scene-native:
	python3 scripts/scene.py test native
test-scene-arm:
	python3 scripts/scene.py test arm
verify-scene:
	python3 scripts/scene.py verify
package-scene:
	python3 scripts/scene.py package

.PHONY: build-video-deps build-video test-video-native test-video-arm verify-video package-video
build-video-deps:
	python3 scripts/video_deps.py native
	python3 scripts/video_deps.py arm
build-video:
	python3 scripts/video.py fixtures
	python3 scripts/video.py build native
	python3 scripts/video.py build arm
	python3 scripts/video_evidence.py build
test-video-native:
	python3 scripts/video.py test native
	python3 scripts/video_playlist_test.py
	python3 scripts/video_evidence.py tested native
test-video-arm:
	python3 scripts/video.py test arm
	python3 scripts/video_evidence.py tested arm
verify-video:
	python3 scripts/video_evidence.py verify
	python3 scripts/video.py verify
	python3 scripts/video_evidence.py seal
package-video:
	python3 scripts/video.py package

.PHONY: test-engine-contract test-engine-adapters check-no-engine-leak test-engine-contract-arm
test-engine-contract:
	python3 scripts/engine_contract.py contract --mode native
test-engine-adapters:
	python3 scripts/engine_contract.py adapters --mode native
check-no-engine-leak:
	python3 scripts/engine_contract.py leak
test-engine-contract-arm:
	python3 scripts/engine_contract.py all --mode arm

.PHONY: test-ui-object-model test-ui-lifecycle test-ui-dirty test-ui-object-arm
test-ui-object-model:
	python3 scripts/ui_object.py object --mode native
test-ui-lifecycle:
	python3 scripts/ui_object.py lifecycle --mode native
test-ui-dirty:
	python3 scripts/ui_object.py dirty --mode native
test-ui-object-arm:
	python3 scripts/ui_object.py all --mode arm

.PHONY: test-layout test-style test-theme test-layout-golden test-ui-schema test-ui-platform-arm
test-layout:
	python3 scripts/ui_platform.py layout --mode native
test-style:
	python3 scripts/ui_platform.py style --mode native
test-theme:
	python3 scripts/ui_platform.py theme --mode native
test-layout-golden:
	python3 scripts/ui_platform.py golden --mode native
test-ui-schema:
	python3 scripts/ui_platform.py schema --mode native
test-ui-platform-arm:
	python3 scripts/ui_platform.py all --mode arm

.PHONY: test-components test-navigation test-overlay test-coffee-component-migration test-components-arm
test-components:
	python3 scripts/ui_components.py components --mode native
test-navigation:
	python3 scripts/ui_components.py navigation --mode native
test-overlay:
	python3 scripts/ui_components.py overlay --mode native
test-coffee-component-migration:
	python3 scripts/ui_components.py coffee_component_migration --mode native
test-components-arm:
	python3 scripts/ui_components.py all --mode arm

.PHONY: test-reactive test-binding test-reactive-budget test-reactive-core-arm
test-reactive:
	python3 scripts/ui_reactive.py reactive --mode native
test-binding:
	python3 scripts/ui_reactive.py binding --mode native
test-reactive-budget:
	python3 scripts/ui_reactive.py reactive_budget --mode native
test-reactive-core-arm:
	python3 scripts/ui_reactive.py core --mode arm

.PHONY: test-virtual-list test-reactive-all-arm
test-virtual-list:
	python3 scripts/ui_reactive.py virtual_list --mode native
test-reactive-all-arm:
	python3 scripts/ui_reactive.py all --mode arm

.PHONY: test-interaction test-interaction-bridge test-interaction-arm
test-interaction:
	python3 scripts/ui_interaction.py interaction --mode native
test-interaction-bridge:
	python3 scripts/ui_interaction.py bridge --mode native
test-interaction-arm:
	python3 scripts/ui_interaction.py all --mode arm

.PHONY: test-gesture test-focus test-interaction-replay
test-gesture test-interaction-replay:
	python3 scripts/ui_interaction.py gesture --mode native
test-focus:
	python3 scripts/ui_interaction.py interaction --mode native

.PHONY: test-framework test-framework-arm test-framework-sanitize verify-framework build-board-framework test-board-framework-package verify-board-framework test-board-framework-verifier
test-framework:
	python3 scripts/framework.py test --mode native
test-framework-arm:
	python3 scripts/framework.py test --mode arm
test-framework-sanitize:
	ASAN_OPTIONS=detect_leaks=1 python3 scripts/framework.py test --mode native --sanitize
verify-framework:
	python3 scripts/framework.py verify
build-board-framework:
	python3 scripts/framework.py package
test-board-framework-package:
	python3 scripts/framework.py package-check
verify-board-framework:
	python3 scripts/framework_hil.py --report "$(REPORT)"
test-board-framework-verifier:
	python3 scripts/framework_hil.py --self-test
