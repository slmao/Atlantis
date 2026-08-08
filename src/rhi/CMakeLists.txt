add_library(atlantis_rhi STATIC
  src/types.cpp
)
add_library(Atlantis::RHI ALIAS atlantis_rhi)

target_include_directories(atlantis_rhi
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_rhi
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)
