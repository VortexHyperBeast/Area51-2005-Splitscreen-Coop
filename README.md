# AREA-51 Split-Screen Co-op

An experimental community co-op project for **Area-51 (2005) on PC**. The goal is to bring two-player local split-screen play to the PC version while building on the modernized engine, compatibility work, and gameplay fixes from **Dreamland-51**.

<blockquote>
  **Current public build:** [Co-op Stage 1 · build 115](https://github.com/VortexHyperBeast/area51-pc/releases/)  
  **Platform:** Windows x64  
  **Mode:** Experimental two-player local split-screen
</blockquote>

[**Download the Windows x64 co-op build**](https://github.com/VortexHyperBeast/area51-pc/releases/download/coop-stage1-run35/Area51-Coop-Stage1-win-x64-run35.zip) · [All releases](https://github.com/VortexHyperBeast/area51-pc/releases) · [Report a bug](https://github.com/VortexHyperBeast/area51-pc/issues) · [Discord](https://discord.gg/7gGhFSjxsq)

---

## What this project is

This project focuses on **local two-player split-screen co-op** for the original 2005 PC game. The published Stage 1 build is experimental and intended for testing; expect limitations and unfinished parts.

This release is a local split-screen build. It should not be confused with the separate LAN and direct-IP multiplayer functionality available in the underlying port.

## Built on Dreamland-51

The co-op project is built on the **Dreamland-51** Area-51 PC source-port foundation, rather than being a remake or a recreation from scratch.

Dreamland-51 modernizes the original PC code for current systems and contributes engine and gameplay work that the co-op project builds upon, including:

- A modern SDL3 platform layer for windowing, audio, video, and input.
- SDL3 gamepad support and updated input handling.
- Modern Direct3D 12 and Vulkan renderer backends.
- Renderer improvements and restored effects from earlier and console versions.
- Camera and aiming fixes.
- Gameplay logic, AI, and movement corrections for high frame rates.

These are improvements from the underlying Dreamland-51 port; they are not a claim that every renderer feature or platform is fully supported by this experimental co-op package.

## Latest release

### Co-op Stage 1 · build 35

[Open the release page](https://github.com/VortexHyperBeast/area51-pc/releases/tag/coop-stage1-run35)

| Detail | Information |
| --- | --- |
| Build | Stage 1, run 35 |
| Package | \`Area51-Coop-Stage1-win-x64-run35.zip\` |
| Target | Windows x64 |
| Players | Two |
| Play mode | Local split-screen |
| Status | Experimental / unofficial |

The release was built from commit [\`b7de01b\`](https://github.com/VortexHyperBeast/area51-pc/commit/b7de01b645866cbab3913571703e5843a656a0f8) by [GitHub Actions run 45465469128](https://github.com/VortexHyperBeast/area51-pc/actions/runs/45465469128).

### Download verification

The published ZIP has this SHA-256 digest:

\`\`\`text
d1ae0720006dd22a04ee89099791ddef5b7144aa65d20b9b9f891f702c740d29
\`\`\`

The release also provides \`A51.pdb\` debug symbols. They are only needed when inspecting a crash report; most players do not need to download them.

## How to install and start

1. Download \`Area51-Coop-Stage1-win-x64-run35.zip\` from the [Stage 1 release](https://github.com/VortexHyperBeast/area51-pc/releases/tag/coop-stage1-run35).
2. Extract the ZIP into a new folder.
3. Open that folder and double-click \`PLAY-COOP-2-PLAYERS.cmd\`.
4. The launcher asks for confirmation before downloading approximately **1.5 GB** of game data. Approve the download only when you are ready.
5. Follow any additional instructions shown by the launcher and the \`README.txt\` included in the ZIP.

**The ZIP does not contain the game data.** The launcher handles the data download after asking for permission.

## Test status and limitations

The Stage 1, build 35 release reports these automated checks:

- **Two-controller test:** passed on the Linux test build using a bot and software rendering.
- **Windows startup check:** the cloud test machine did not have a graphics card and attempted to start the build with a software Vulkan driver. This does not establish compatibility across physical GPUs and driver versions.
- **Test code:** the release build job checked that the playable build did not contain test code.

This is an early experimental build. Consult the \`README.txt\` inside the release ZIP for the current list of known gaps and any build-specific notes. Do not assume that unlisted platforms or configurations have been tested.

## Troubleshooting and bug reports

When reporting a problem, include as much of the following as possible:

- The release name and build number (\`Co-op Stage 1, build 35\`).
- Windows version and whether the issue happens at launch or during play.
- GPU model and graphics driver version.
- Controller models and how they are connected.
- Steps to reproduce the issue, plus screenshots, logs, or crash details where available.

Open an issue in the [GitHub issue tracker](https://github.com/VortexHyperBeast/area51-pc/issues) or discuss it with the community on [Discord](https://discord.gg/7gGhFSjxsq).

## FAQ

### Is this an official Area-51 release?

No. This is an unofficial community project. It is not affiliated with or endorsed by the original developers or publishers.

### Is this online or LAN co-op?

The published Stage 1 package is for **local two-player split-screen**. The underlying Dreamland-51 port has separate LAN and direct-IP multiplayer functionality, but that should not be confused with this split-screen release.

### Does the ZIP include the original game data?

No. The launcher asks before downloading approximately 1.5 GB of game data.

### Is the split-screen build available for Linux or Android?

The current published Stage 1 co-op package is Windows x64. The underlying Dreamland-51 source port supports other platforms, but this does not mean this experimental split-screen package is available or tested on them.

### Where can I find the known issues?

Check the \`README.txt\` included in the release ZIP and the [release notes](https://github.com/VortexHyperBeast/area51-pc/releases/tag/coop-stage1-run35).

---

## Credits and project background

This co-op work stands on the Dreamland-51 source port and its ongoing preservation, modernization, compatibility, renderer, input, and gameplay-fix work. Thanks to the people contributing to the original source-port foundation, testing, and the co-op effort.

*Area-51* (2005), its original code and assets, names, and trademarks remain the property of their respective rights holders. This independent community project does not grant rights to the original game or its assets.
