add_executable(atlantis_foundation_demo main.cpp)

target_link_libraries(atlantis_foundation_demo
  PRIVATE
    Atlantis::Core
    atlantis_compiler_warnings
)
