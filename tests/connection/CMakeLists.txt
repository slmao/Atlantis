# Plan 0054 M2/M3: Atlantis Connection -- the connection against the boundary it
# forwards to, its per-connection subscriptions and failure routing, and the
# text forms. Cooks and bakes its own fixture scene (Asset System and World
# reach this test target through Atlantis::Connection's PUBLIC World link).
add_executable(atlantis_connection_tests
  connection_tests.cpp
)

target_link_libraries(atlantis_connection_tests
  PRIVATE
    Atlantis::Connection
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

catch_discover_tests(atlantis_connection_tests DISCOVERY_MODE PRE_TEST)
