# Contributing to CTR Reload Edition

Thanks for wanting to help! Contributions come in as pull requests from a
fork. This page lists the rules; how to build is in [BUILDING.md](BUILDING.md).

## How development works

Development happens openly on the `dev` branch, in batches and without fixed
dates. Pull requests are reviewed and merged in batches too, so an answer can
take a while. `main` holds the releases; a new version comes out when enough
has come together.

For a bigger change, please open an issue first, so we can agree on the idea
before you put work into it.

## Pull requests

1. Fork the repository and create a branch from `dev`.
2. Make your change, build it and test it (see [Build and test](#build-and-test)).
3. Open the pull request against `dev` - never against `main`.

One topic per pull request: a fix and an unrelated clean-up are two pull
requests. The pull request template has a short checklist.

## No game data - ever

Nothing from the game goes into this repository, in any file, commit or
branch. That means no:

- disc images (`.bin`, `.cue`, `.iso`, ...) and nothing extracted from them
  (`BIGFILE.BIG`, `KART.HWL`, the `assets` folder)
- files in the game's formats (`.lev`, `.vrm`, `.mpk`, `.tim`, `.xa`, `.str`,
  `.vag`, `.lng`, ...), whole or in parts
- track or character containers and what goes into them (`.rldtrack`,
  `.rldchar`, `.rldprev`, `.sca`, `.sndb`, `.cseq`)
- patches for the game (`.xdelta`, `.ppf`, `.ips`, `.bps`)
- memory cards, quick states, replays or memory dumps
- screenshots, videos or sound recordings with game content
- logs or other output that contain data from the game
- data tables, texts or other data copied from the game: values you extract
  from your disc count as game data, also when you paste them into a source
  file as a C table
- content by other people: custom tracks, music, characters, textures, fonts
- 3D models, voices, sound effects and images made by other people

This rule is about data: decompilation work on the game's code is welcome.
The retail data tables and texts already in the code come from the upstream
decompilation projects (see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)).

**Why so strict:** once a file is in a public pull request, it stays in this
repository's history on GitHub - also when the pull request is closed without
merging. We cannot take it back. This already starts with a push to your
fork, because forks are public too.

The automatic check `content-guard` fails if any commit of a pull request
contains a game data file type, a 3D model, audio, picture or archive file
type (`.ply`, `.obj`, `.fbx`, `.blend`, `.gltf`, `.wav`, `.mp3`, `.ogg`,
`.png`, `.jpg`, `.bmp`, `.zip`, `.7z`, ...) or a binary file over 1 MB. These
types fail even if you made the file yourself; the only exceptions are the
pictures and sounds of the bundled SDL copy, which `tools/content_guard.py`
lists one by one with their exact content. The local `pre-push` hook (see
[Local hooks](#local-hooks)) runs the same check and more before anything
leaves your machine. No check catches everything, so please look at what you
commit. If a test needs a file, generate it in code, as the self-tests do
(`platform/native_testfiles.c`).

Screenshots in an issue or a pull request comment are fine, just not as files
in the repository.

## English only

Everything in this repository is English: code, comments, file and folder
names, commit messages, and the pull request title and text. Check it with:

    python tools/lang_check.py

It also runs as one of the self-tests and in the local hooks.

## Commit and pull request titles

Commit titles are the public change log: the first line of every commit on
`dev` is listed in the notes of the nightly build. Keep them and the pull
request title short, clear and in English, and say what changed, for example
`Language check: file and folder names`. Details go into the commit message
body and the pull request text.

## Build and test

You need Visual Studio 2022, CMake, the Vulkan SDK and Python; the details are
in [BUILDING.md](BUILDING.md). From a command prompt in the repository root:

    build-msvc.bat

This builds the Release configuration in `build-msvc-x86\` and runs the
self-tests; they need no game data. Your pull request should build with 0
warnings and pass all self-tests. The same command runs automatically on every
pull request.

To test a change in game you need your own disc image (see
[Getting started](README.md#getting-started)). Write in the pull request what
you tried.

## Local hooks

Enable the hooks once per clone:

    git config core.hooksPath tools/git-hooks

- `pre-commit` and `commit-msg` run the language check on the staged files and
  on the commit message.
- `pre-push` checks every commit the push would send and refuses the push if
  one of them contains, among other things, anything `content-guard` refuses,
  a file in a forbidden folder (such as `assets` or `tracks`) or any file
  larger than 5 MB. Like the other hooks, it needs Python 3.

## License

CTR Reload Edition is licensed under the
[GNU General Public License v3.0](LICENSE). By opening a pull request you
agree that your contribution is licensed under the same license, and you
confirm that you have the right to contribute it. Code from another project
is welcome only under a license that is compatible with this one: name the
project and its license in the pull request and add it to
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Security problems

Please do not report them in a public issue - see [SECURITY.md](SECURITY.md).
