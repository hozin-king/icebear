# 🐻‍❄️ Icebear

A free, open-source alternative to [Tango](https://www.amanieusystems.com/):
a system that lets 32-bit-only (`armeabi-v7a`) Android apps run on 64-bit-only
(`arm64-v8a`) devices, root required.

Icebear has no affiliation with Amanieu Systems and contains none of their
code. It's an independent, from-scratch project.

## 🤔 Why this exists

Some Android apps never got a 64-bit build: abandoned by their developer,
or just never updated. That was fine as long as every 64-bit chip could
still run 32-bit ARM code in hardware. Newer chips (Snapdragon 8 Gen 3 and
up) dropped that hardware support entirely, so those apps don't run slowly
on new phones, they **don't run at all**. Icebear exists to fix that, for
free, in the open. See `docs/TERMS.md` for exactly what this is (and isn't)
meant for.

## 🧪 Status

Early and honest about it. This is a passion project, not a finished
product; see `docs/ARCHITECTURE.md` for what's actually implemented today
versus what's still a design sketch, and `docs/CONTRIBUTING.md` if you
want to help push it forward.

## ⚙️ How it works, briefly

Rather than patching every APK individually, Icebear implements Android's
own [Native Bridge](https://android.googlesource.com/platform/art/) interface
(the same mechanism Google's NDK Translation and Intel's Houdini use to run
ARM apps on x86 Chromebooks) as a Magisk / KernelSU / KernelSU-Next / APatch
module. Once installed, 32-bit-only apps become launchable device-wide.

The on-device UI is a WebUI built into the module itself (opened from your
root manager app, no separate app to install) for picking an app, checking
it, and installing it. There's also a small Windows/macOS desktop app for
inspecting an APK from a PC.

Android isn't the only place this problem shows up: a 64-bit-only Linux
system can't run a 32-bit ARM binary either, for the same hardware reason.
`linux/` is an early, standalone (no root, no Android) take on that, see
its own README for what's real versus a proof of concept so far.

Full explanation, including why a module + WebUI rather than a standalone
app or per-APK patching, in `docs/ARCHITECTURE.md`.

## ✨ What's better here

- The WebUI ships without a build step, remembers your last checked app,
  shows device ABIs and bridge state as live status chips, and guards
  against double-running a check.
- One translator binary, `libicebear_translator.so`, for Magisk, KernelSU,
  KernelSU-Next, and APatch alike.

## 🛠️ Build the flashable module yourself

Everything you need is two scripts away, on **both Linux and Windows**
(Windows via Git Bash / MSYS2 / Cygwin):

```bash
# 1. Download a portable toolchain (NDK, CMake, Ninja, JDK 21) into
#    build/tools/. No admin rights, no package manager needed.
bash scripts/setup_deps.sh

# 2. Build the translator and package the flashable zip in one step:
bash scripts/build_module.sh
```

Result lands at `build/icebear-module.zip` — flash it in Magisk, KernelSU,
KernelSU-Next, or APatch.

- `setup_deps.sh` accepts `--force` (re-download everything) and
  `--skip-jdk` (JDK is only needed for the `:core`/`:desktop` Gradle
  builds, not for the module zip).
- Already have your own NDK/CMake? Skip `setup_deps.sh` entirely and just
  run `build_module.sh`; it uses whatever's on your `PATH` as long as
  `ANDROID_NDK_HOME` is exported and `cmake` is reachable.

## 📁 Repository layout

```
core/     shared Kotlin: APK inspection, compatibility checks (used by desktop/)
desktop/  Windows/macOS APK-inspection helper app (Compose Desktop)
native/   the ARM32 -> ARM64 translator for Android (C, CMake)
linux/    early standalone ARM32 -> ARM64 support for Linux itself (C, CMake)
module/   the Magisk/KernelSU/KernelSU-Next/APatch module, including the WebUI
docs/     everything below
scripts/  build helper scripts
```

## 🙏 Credits

Icebear is a rebrand and polish of [Mango](https://github.com/OWNER/mango),
which did the heavy lifting: the ARM32 → ARM64 translator core, the native
bridge shim, the module + WebUI architecture, and the cross-root-manager
support. Without Mango's groundwork, Icebear wouldn't exist. It remains
GPL-3.0, same as its parent.

## 📚 Docs

See [`docs/README.md`](docs/README.md) for how to navigate the rest of the documentation.

## 📜 License

GPL-3.0, see [`LICENSE`](LICENSE). Free as in freedom, no ads, nothing to
buy, ever.