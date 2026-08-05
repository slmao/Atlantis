add_executable(atlantis_platform_demo main.cpp)

target_link_libraries(atlantis_platform_demo
  PRIVATE
    Atlantis::Platform
    atlantis_compiler_warnings
)
