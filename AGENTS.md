# Creation Texture

This project is generated from the Creation Suite shell template.

## Intent

Keep this repo aligned with shared suite infrastructure:

- shared UI shell
- shared AI/service wiring
- shared asset and project-container model
- shared interop and registry rules

## Local Rules

- preserve the shared shell unless the project is intentionally diverging
- keep domain-specific behavior in `Source/` and `Language/`
- prefer consuming shared suite libraries over copying shared code locally

## LLVM / vcpkg / C: Drive Rules

- **Never build, rebuild, or touch LLVM — directly or as a side effect of any `vcpkg` command (including `vcpkg install` in manifest mode) — without an explicit, in-the-moment yes from the user.** If blocked, stop and ask.
- **Under no circumstances write, download, build, or install anything on the C: drive. Period.**
- See the root `AGENTS.md`'s LLVM / vcpkg Build Rule and C: Drive Rule for the full incident these are based on.

