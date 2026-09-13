Plan 0034 Milestone 6 (ADR-0078): libVkLayer_khronos_validation.so for
arm64-v8a is bundled here, per ADR-0078's own Decision.

Source: C:\Users\slmao\android-tools\vvl-android\android-binaries-1.4.357.0\arm64-v8a\libVkLayer_khronos_validation.so
Version: 1.4.357.0 -- matches the desktop Vulkan SDK (C:\VulkanSDK\1.4.357.0)
already used to build and verify every Windows-side milestone in this
Plan, so the same validation ruleset applies on both platforms.

This closes the gap Milestone 4/5's own README (superseded by this file)
disclosed: as of that Milestone, no such binary existed anywhere in this
environment's installed NDK or desktop Vulkan SDK. It was since obtained
and placed under android-tools/vvl-android/ outside this repository
(pre-provisioned for this Milestone, not fetched by this Plan's own
automation) and copied here unmodified.
