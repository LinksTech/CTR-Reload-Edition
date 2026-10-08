# Versioning

CTR Reload versions have the form `MAJOR.MINOR.PATCH`, shown as for example
`0.7.5 Beta` in the game, in `--version`, in Reload Studio and in the package name.

- Regular releases advance in steps of 0.0.5: 0.7.5, 0.8.0, 0.8.5, 0.9.0, 0.9.5.
- After 0.9.5 comes 0.10.0, then 0.10.5, and so on.
- A hotfix between two releases adds 0.0.1 to the release it fixes, for example 0.7.6.
- 1.0.0 is set deliberately as a milestone; counting up never reaches it by itself.
- Older tags (`v0.0-beta0`, `v0.0.5`) keep their names.

The single source is `CTR_NATIVE_VERSION` in `CMakeLists.txt`. Kept in step by
hand: `CTR_RELOAD_VERSION` in `main.c`, `tools/reloadstudio/reloadstudio.rc`
and the fallback defines in `tools/reloadstudio/reloadstudio.h` and `tools/rldpack.c`.
