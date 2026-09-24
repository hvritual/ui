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

.PHONY: check-engine-spike fetch-engine-spike build-engine-spike-native build-engine-spike-arm test-engine-spike-native test-engine-spike-arm test-engine-spike verify-engine-spike
check-engine-spike:
	python3 scripts/engine_spike.py check
fetch-engine-spike:
	python3 scripts/engine_spike.py fetch
build-engine-spike-native:
	python3 scripts/engine_spike.py build native
build-engine-spike-arm:
	python3 scripts/engine_spike.py build arm
test-engine-spike-native: build-engine-spike-native
	python3 scripts/engine_spike.py test native
test-engine-spike-arm: build-engine-spike-arm
	python3 scripts/engine_spike.py test arm
test-engine-spike: test-engine-spike-native test-engine-spike-arm
verify-engine-spike:
	python3 scripts/engine_spike.py verify
