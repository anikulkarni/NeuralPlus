# Contributing

Thank you for considering a contribution to NeuralPlus.

## Before opening code changes

For provider integrations, public API changes, new dependencies, or substantial architecture work, open a discussion or issue first. This avoids parallel designs and makes review criteria explicit.

Small documentation fixes and narrowly scoped bug fixes can go directly to a pull request.

## Development setup

```bash
cmake -S . -B build \
  -DNEURALPLUS_BUILD_TESTS=ON \
  -DNEURALPLUS_BUILD_EXAMPLES=ON \
  -DNEURALPLUS_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Design expectations

Contributions should preserve these properties:

- C++17 compatibility unless a separately guarded feature requires newer C++
- Clear ownership and lifetime semantics
- Provider-neutral core APIs
- Thread safety where parallel tool execution reaches the code
- Minimal mandatory dependencies
- Useful errors without leaking credentials or sensitive content
- Tests for behavior, not just compilation
- Documentation for public interfaces and non-obvious invariants

## Style

- Use the repository `.clang-format` configuration.
- Prefer small classes and focused functions.
- Use templates when they improve type safety or remove repetition, not only to avoid virtual dispatch.
- Avoid macros except for portability or build integration.
- Keep public headers self-contained.
- Use `std::string_view` for non-owning string inputs when lifetime is unambiguous.

## Pull requests

A pull request should include:

- A clear problem statement
- The chosen approach and important alternatives
- Tests or an explanation of why tests are not applicable
- Documentation updates for public behavior
- Portability impact, especially for Windows and enterprise Linux

By contributing, you agree that your contribution is licensed under the repository's MIT License.
