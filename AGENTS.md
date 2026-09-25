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

