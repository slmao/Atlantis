# See Plan 0008 Section 1 for this target's dependency graph:
# atlantis_shader_compiler links neither RHI nor the RHI-integration
# target -- Tools never constructs a PipelineCreateParams or touches an
# RHI type.
add_executable(atlantis_shader_compiler
  main.cpp
)

target_link_libraries(atlantis_shader_compiler
  PRIVATE
    Atlantis::ShaderSystem
    Atlantis::Core
    atlantis_compiler_warnings
)
