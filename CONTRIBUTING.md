<!--
Copyright 2026 Aniket Kulkarni
SPDX-License-Identifier: Apache-2.0
-->

# Contributing

Thank you for considering a contribution to NeuralPlus.

Open a discussion or issue before provider protocols, public API changes, new
mandatory dependencies, or substantial architecture work. Narrow bug fixes and
documentation corrections can go directly to a pull request.

Participation in the project is governed by the
[Code of Conduct](CODE_OF_CONDUCT.md).

## Development

```bash
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

The `dev` preset builds all examples as part of the normal build. To build only
the examples, run `cmake --build --preset dev-examples`; the executables are
written to `build/dev/examples/`.

Use the repository `.clang-format` configuration for C++ changes. Public
headers must be self-contained and remain C++17-compatible.

## Design expectations

- Keep one orchestration path in `AIClient`; providers translate one round.
- Prefer `FunctionTool` and `FunctionTracer` before adding subclasses.
- Keep model names and capabilities in data, not new model-name classes.
- Keep convenience model configurations small, sourced from official provider
  documentation, and overrideable as ordinary provider configs.
- Use clear ownership and small runtime interfaces.
- Use ordinary templates only where they provide direct type safety; avoid
  template-metaprogramming machinery.
- Preserve session and tool concurrency invariants.
- Never expose credentials or unredacted model/tool content in errors, traces,
  tests, or fixtures.
- Add deterministic protocol tests using `MockHttpTransport`; default tests
  must not require API keys or live provider calls.
- Runnable provider examples must instantiate the real client and production
  transport. Give each a `--help` path that exits before credential resolution
  so CI can smoke-test it safely.
- Document public behavior and portability impact.

## Pull requests

Include:

- the problem and chosen approach;
- tests, or why none apply;
- documentation for public behavior;
- migration guidance for breaking changes; and
- any Linux, macOS, or Windows impact.

By contributing, you agree that your contribution is licensed under the
repository's [Apache License 2.0](LICENSE). Accepted contributors are
recognized in repository history, release notes, and
[CONTRIBUTORS.md](CONTRIBUTORS.md).
