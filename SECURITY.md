# Security policy

## Reporting a vulnerability

Please report security problems privately - not in a public issue, pull
request or comment:

1. Open the **Security** tab of this repository.
2. Choose **Report a vulnerability**.
3. Describe the problem: what it affects, the version
   (`ctr_native.exe --version`), the steps to reproduce it, and what an
   attacker could do with it.

This uses GitHub's private vulnerability reporting: only you and the
maintainers of this repository can see the report. Please do not attach disc
images or other game data, not even there; describe the file, or how to make
it, instead.

We answer in the report as soon as we can; there are no fixed response times.
Please give us time to fix the problem before you make it public. A fix goes
into `dev` and the nightly build first, and then into a release. If we
publish an advisory, we name you in it as the finder if you want.

## Scope

In scope:

- `ctr_native.exe` and `alphamaker.exe` (with the built-in `rldpack` packer),
  as built from this repository - for example a crafted track container, cup
  list, settings file or disc image that makes a program run code, or read or
  write files outside its folders
- the build and release pipeline: the workflows in `.github/workflows`,
  `build-msvc.bat`, the package script, and the files attached to releases and
  to the nightly build

Out of scope:

- quick states and replays (developer switches behind `--dev`): they are raw
  memory snapshots of the game and are not safe to load from someone else by
  design - load only files you made yourself
- problems in third-party components that do not come from how this project
  uses them - please report those to their projects
- crashes and bugs without a security impact - please open a normal
  [issue](https://github.com/LinksTech/CTR-Reload-Edition/issues)

## Supported versions

| Version | Security fixes |
| --- | --- |
| Latest release | yes |
| Nightly build | yes |
| Older releases | no - please update |

## No bounty

CTR Reload Edition is a non-commercial fan project. There is no bug bounty
and no payment for reports.
