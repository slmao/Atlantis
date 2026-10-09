# Plan 0058 P7 (Spec 0058 R1, R7; ADR-0112 D4, ADR-0113 D2): the Atlantis C#
# SDK's library, built by `dotnet build` (cmake/AtlantisDotnet.cmake). Its
# generated bindings (Atlantis.Gameplay/Generated/World.g.cs) are committed,
# never generated at build time.
atlantis_add_dotnet_project(
  NAME atlantis_csharp_gameplay
  PROJECT "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Gameplay/Atlantis.Gameplay.csproj"
  SOURCE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/Atlantis.Gameplay")
set(ATLANTIS_CSHARP_GAMEPLAY_DLL "${atlantis_csharp_gameplay_DLL}" PARENT_SCOPE)
