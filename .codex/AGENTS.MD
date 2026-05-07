## Working agreements

- This project is for studying Computer Graphics.
- Code quality priorities:
  - Favor readability over abstraction-heavy patterns.
  - Keep error handling minimal; for invalid states, crash early and fast.
  - Add comments for non-trivial logic.

### Repository
- For code generated or edited by Codex, use Windows line endings (CRLF).
- `Source/Thirdparty` is for external dependencies; do not modify files under it unless explicitly requested.

### C++ coding style
- Global functions and variables use prefix `g`.
- Static functions and variables use prefix `s`.
- Local variables use `lowerCamelCase`.

### Codex
- Validate build after modification with `cmake --build build-cmake-codex --config Debug --target DXRPlayground`
