# Developer Documentation

This is the technical documentation for C-ChessEngine — how it's built internally, not how to play it. For build/install/gameplay instructions, see the [top-level README](../README.md).

Start with [**ARCHITECTURE.md**](ARCHITECTURE.md) for the big picture, then dig into whichever subsystem you're working on:

| Doc | Covers | Source files |
| --- | --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Module map, data flow, key invariants, build system | everything |
| [BOARD_AND_RULES.md](BOARD_AND_RULES.md) | Board representation, move application, check detection, draw rules | `structs.h`, `game.h`/`game.c` |
| [SEARCH_AND_EVAL.md](SEARCH_AND_EVAL.md) | NegaMax search, alpha-beta, quiescence, iterative deepening, time management, evaluation | `ai.h`/`ai.c`, `eval.h`/`eval.c` |
| [NOTATION_AND_FORMATS.md](NOTATION_AND_FORMATS.md) | Long algebraic, SAN, FEN, PGN, and the older custom save format | `notation.h`/`notation.c`, `fileio.h`/`fileio.c` |
| [ONLINE_PLAY.md](ONLINE_PLAY.md) | Lichess Board API integration, bot mode, chess clocks | `lichess.h`/`lichess.c`, `timecontrol.h`/`timecontrol.c` |
| [UCI.md](UCI.md) | UCI protocol front end, for driving the engine from cutechess/Arena/etc. | `uci.h`/`uci.c` |
| [TESTING.md](TESTING.md) | Test harness design, what's covered vs. not, how to add a test | `tests/` |

Every header file (`include/*.h`) also carries a `@file` docstring at the top pointing back to the relevant doc here, and every exported function has its own Doxygen comment. If you have Doxygen installed, `make docs` generates browsable HTML from those comments into `build/docs/html/` (see [ARCHITECTURE.md](ARCHITECTURE.md#generated-api-docs)).
