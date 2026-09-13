Plan 0034 Milestone 4/6 (ADR-0078): this directory is where
libVkLayer_khronos_validation.so for arm64-v8a belongs, per ADR-0078's
own Decision ("bundled as the prebuilt .so layer binaries the Android
NDK/Vulkan SDK already ships... placed under the Gradle project's
jniLibs/arm64-v8a/").

As of this Milestone's own implementation (2026-09-13), no such binary
exists anywhere in this environment's installed Android NDK
(29.0.14206865) or desktop Vulkan SDK (1.4.357.0) -- confirmed by
searching both trees for VkLayer_khronos_validation.so and any
Android-specific Vulkan-ValidationLayers directory; neither ships one.

Per ADR-0078's own pre-authorized fallback ("If this bundling proves not
to work cleanly on the actual verification device/emulator, Spec 0034's
Risks section already requires that gap be disclosed explicitly in the
implementing PR rather than silently weakening the 'Validation Layers
clean' bar"): this is that disclosure. This directory is intentionally
empty of any binary right now.

Consequence: RuntimeApplication's BootstrapConfig::enableValidationLayers
is still set true (android_main.cpp, matching main.cpp's own Windows
behavior and ADR-0078's "no new enable mechanism" design) -- on a real
device today, Vulkan instance creation will fail with
DeviceCreateError::ValidationLayerUnavailable (vulkan_instance.cpp) until
this gap is closed, since VK_LAYER_KHRONOS_validation will not be found.
This is out of scope for Plan 0034 Milestones 4/5 (no on-device run is
attempted or required there); Milestone 6's own on-device verification
must source this binary (e.g. from the Vulkan-ValidationLayers GitHub
releases' prebuilt Android artifacts) before it can proceed, or must
disclose running without it as its own limitation.
