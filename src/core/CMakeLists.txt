add_library(atlantis_core STATIC
  src/log.cpp
  src/assert.cpp
)
add_library(Atlantis::Core ALIAS atlantis_core)

target_include_directories(atlantis_core
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_core
  PRIVATE
    atlantis_compiler_warnings
)
