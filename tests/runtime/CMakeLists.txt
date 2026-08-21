# Plan 0013 Step 1: GPU-independent tests only -- no Device, no GPU, and
# no real window required to run. atlantis_runtime_gpu_tests (the real
# windowed smoke test) is added in Step 5.
add_executable(atlantis_runtime_tests
  lifecycle_state_tests.cpp
  exit_reason_tests.cpp
  error_classification_tests.cpp
)

target_link_libraries(atlantis_runtime_tests
  PRIVATE
    Atlantis::RuntimeHost
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_runtime_tests DISCOVERY_MODE PRE_TEST)
