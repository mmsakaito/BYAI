# Repository Guidelines

## Project Structure & Module Organization

This repository is currently a documentation and planning baseline for an ESP32-S3 voice terminal. Firmware source, tests, and binary assets have not been committed yet. Keep repository-level guidance in `README.md`, contribution rules in `CONTRIBUTING.md`, and GitHub templates in `.github/`. When firmware arrives, keep board-specific configuration, drivers, application code, tests, and assets in clearly named top-level directories; document each new layout in the README.

## Build, Test, and Development Commands

No build system or automated test suite is tracked yet. Do not invent commands or claim a build passed. For documentation-only changes, run:

```bash
git diff --check
git status -sb
```

When ESP-IDF or another toolchain is added, record the exact configure, build, flash, monitor, and test commands in `README.md` and verify them on the target board.

## Coding Style & Naming Conventions

Follow the style already present in the edited file. Use UTF-8 Markdown, descriptive headings, and short task-oriented lists for project documentation. Name new branches with lowercase Conventional Commit types:

```text
feat/audio-output
fix/wifi-reconnect
docs/repository-guidelines
```

Use a concise scope and hyphenated description. Do not add a formatter or linting rule until the firmware language and build tooling are established.

## Testing & Hardware Verification

Record the tested board, firmware version, key configuration, commands, logs, and result for hardware changes. For audio, display, touch, Wi-Fi, power, and service changes, include the relevant device or service verification in the linked Issue. Investigation and spike tasks must keep reproducible steps and conclude whether the approach is feasible, partially feasible, or infeasible.

## Commit & Pull Request Guidelines

Use signed Conventional Commits, for example `feat(audio): 实现 WM8978 I2S 音频输出` or `docs(repo): 补充仓库说明`. Add `Refs #123` in the body when applicable. Create normal work branches from `dev`, open PRs to `dev`, and merge verified `dev` changes into `main` by PR only. Use the PR template: describe the change, link the Issue, list verification, state impact, and attach logs, screenshots, recordings, or test data when relevant.
