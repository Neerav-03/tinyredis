# tinyredis: notes for Claude

- Learning project: the owner is learning Redis and systems programming while building it.
  Every milestone ships with a write-up in `docs/learn/NN-*.md` (concept, design choices,
  "try it", interview questions). Explain why, not just what.
- Build and run on Linux only. On the owner's Windows machine, run commands inside WSL:
  `wsl -d Ubuntu -- bash -s <<'EOF' ... EOF` with `cd "/mnt/c/Users/Neerav Daswani/Documents/SourceCode/tinyredis"`.
- Before committing, run `cmake --build --preset debug && ctest --preset debug && python3 -m pytest tests/e2e`.
  The `asan` preset must pass too. CI on GitHub Actions is the source of truth.
- Style: C++20, `.clang-format` (Google, 100 columns), RAII for every resource, no raw `new`/`delete`.
  Code lives in `namespace tinyredis::<module>`; everything except `main.cpp` goes into `tinyredis_core`.
- Behaviour should match real Redis. When unsure, check against `redis-server`, which is installed in WSL.
- Changes go through PRs; `main` is protected and needs the 4 CI checks green. When a milestone lands:
  update the README status table, tag the merge commit `mN` (annotated) and push the tag. Learning notes
  link to code at their own tag, so they stay accurate as the code moves on.
- No performance numbers in the repo until the benchmark milestone measures them properly.
