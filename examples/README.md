# examples/

Non-shipping demonstration programs — not part of the Atlantis runtime
architecture. Kept out of `src/` deliberately so a smoke-test/demo
program can never be mistaken for a shipping engine module; see
[ADR-0010](../adr/0010-cmake-structure.md).

- **`foundation_demo/`** — proof-of-build executable for
  [specs/0001-project-foundation.md](../specs/0001-project-foundation.md).
  Links `Atlantis::Core` and exercises the logging and assertion
  abstractions. Not a preview of Atlantis Runtime.
