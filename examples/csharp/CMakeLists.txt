# Plan 0058 P8 (Spec 0058 R5, ruling Q6): the C# beacon demo, built by
# `dotnet build` after the library (cmake/AtlantisDotnet.cmake). Run it as
# `dotnet <build>/csharp/<config>/GameplayDemo/GameplayDemo.dll`.
atlantis_add_dotnet_project(
  NAME atlantis_csharp_gameplay_demo
  PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/GameplayDemo/GameplayDemo.csproj"
  SOURCE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/GameplayDemo" "${CMAKE_SOURCE_DIR}/src/csharp/Atlantis.Gameplay"
  DEPENDS atlantis_csharp_gameplay)
set(ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL "${atlantis_csharp_gameplay_demo_DLL}" PARENT_SCOPE)
