# Default-branch consolidation

The fork's default branch is `main`. The verified macOS history through
`eebefeb` and the earlier experimental `macos-native` history are merged, not
squashed, rebased, or force-pushed. The existing `linux-port` history was already
an ancestor. Old branches are retained; new development continues on `main`.

Conflict resolution retained the verified setup/runtime/performance code.
Distinct early work was adapted rather than discarded:

- `Setup-macos.command` and native Homebrew PATH bootstrapping for Finder.
- The lift driver's nonzero exit on translation errors and optional local toolkit
  discovery. The tested `../tools` checkout remains the first default.
- Synthetic PE fixtures exercising the real native host, both lifted DLLs,
  named/ordinal exports and nested guest calls, plus four runtime self-tests.
- Read-only GitHub Actions checks on standard Apple Silicon and Intel Mac runners.
  They use only source and synthetic fixtures, never proprietary game files.

The identical obsolete `pcrecomp-revision.txt` pin was consolidated into the
existing `.pcrecomp-revision`. Older validation text is superseded by the
verified ledgers rather than presented as a second authoritative setup.

At consolidation, the local Apple Silicon build passed **74 Python tests and
15 native tests**, including all five tests adapted from the early branch.
Configured Intel CI does not by itself establish Intel gameplay compatibility.

The original Windows/Linux installer workflow is unchanged. No release tag,
proprietary binary, game asset, font, save or generated game C was published.
