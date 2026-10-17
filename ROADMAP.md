# Roadmap

## Next

- Play at the console, windowed and full screen, through offstage
  (every run so far was headless over RDP).
- A harness milestone for "a turn ended", read from the game's turn counter
  once its address is known.
- A longer scripted game: several turns, a save and a load.
- Capture the Bink intro in `--record` (binkw32 presents on its own path).

- Linux: the play tests, the conformance harness and the oracle against
  `build-linux/civ3`; redraw the window while a load runs without pumping.

## Later

- Replace the `native32` bridge for jgl.dll with an SDL2 or D3D presenter, so
  scaling and windowed mode stop depending on 1990s GDI mode switching.
- The 59 bodies with no terminator: check each against the catalog (the
  last two unresolvable `ITAIL` labels went with the current pcrecomp's lift).
- Play the Hall of Fame, scenarios and saves from the 2001 original and
  *Play the World* through Conquests' own loaders.

## Out of scope

- Retail disc builds: different executables with disc protection.
- Multiplayer over GameSpy/DirectPlay lobbies that no longer exist.
- Redistributing any game file or any generated source.
