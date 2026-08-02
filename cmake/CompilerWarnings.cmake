# Shared warnings-as-errors interface target. See ADR-0010.
add_library(atlantis_compiler_warnings INTERFACE)

if(MSVC)
  # /utf-8: source files are UTF-8; without this MSVC guesses based on the
  # active Windows code page, which misreads non-ASCII characters (e.g. an
  # em dash in a comment) as an error under /WX.
  target_compile_options(atlantis_compiler_warnings INTERFACE /W4 /WX /utf-8)
else()
  target_compile_options(atlantis_compiler_warnings INTERFACE -Wall -Wextra -Werror)
endif()
