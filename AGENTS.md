# Creation Texture

This project is generated from the Creation Suite shell template.

## READ FIRST: owner requirements

**[docs/REQUIREMENTS.md](docs/REQUIREMENTS.md) is the owner's requirements for this app. Read it before
changing anything here.** Where code disagrees with it, the code is wrong. Do not rename nodes, move
controls, or change workflows it describes without the owner's say-so. In short: no general asset browser;
an asset is always chosen through the node that uses it; selecting any node shows it in a dockable
Properties panel where every value editable in that situation is editable (no double-click).

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

