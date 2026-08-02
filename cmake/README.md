# cmake/

Empty by design.

CMake module organization, dependency management strategy (vcpkg, Conan,
`FetchContent`, or submodules), and target structure are build-system
decisions explicitly deferred to their own spec/ADR — see
[docs/process/ci-strategy.md](../docs/process/ci-strategy.md) ("Open
questions requiring human/spec decisions") and
[AGENTS.md](../AGENTS.md). No build system exists yet; do not add CMake
modules here without a linked spec and plan.
