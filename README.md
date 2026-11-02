# DCTelnet
[![2.0-UP/X release](https://img.shields.io/github/v/release/spotUP/dctelnet?include_prereleases&color=orange&label=unofficial%20pre-release)](https://github.com/spotUP/dctelnet/releases)
![AmigaOS 2.0+](https://img.shields.io/badge/AmigaOS-2.0+-blue)
![Motorola CPU 68000+](https://img.shields.io/badge/CPU-68000+-blue)


> **This is DCTelnet 2.0-UP/X, a special unofficial release.**
> It is built by spotUP from the work in progress for DCTelnet 2.0
> (Bruno Frederic's `v2.0-dev` branch), with extra features and fixes on top.
> It is not an official DCTelnet release and is not on Aminet.
>
> For the official DCTelnet, go to
> [bruno-frederic/dctelnet](https://github.com/bruno-frederic/dctelnet/releases)
> or [Aminet](https://aminet.net/package/comm/tcp/DCTelnet).
> Please do not report problems found in this build to the official project.
> See [New in 2.0-UP/X](#new-in-20-upx-unofficial-release) for what is different.

**DCTelnet** is a Telnet client for **Amiga** computers.
It is **easy to use**, **lightweight**, and **reliable**.

In fullscreen mode, DCTelnet is ideal for accessing **Telnet-enabled BBS systems** on classic
**Amiga hardware**, and supports **file transfers via ZModem**.

It is also perfect for regular Telnet usage thanks to its **windowed mode on Workbench**.


![DCTelnet screenshot](assets/screenshot.png)

---

> **Note:** Starting with the v1.7 release in 2026, this repository provides
> recompiled versions of DCTelnet, including a **68000‑compatible binary**
> for classic Amiga systems (A500, A600, A2000 with KS 2.0 or higher), as
> well as an **optimized 68020 build**.
>
> **DCTelnet** was originally written by **Zed**. His latest releases
> (v1.5-1.6, published between 1998 and 2000) were compiled
> **for 68020+ CPUs only**.
>
> The new releases also offers **simplified installation**, **improved
> documentation** and **bug fixes**.

---

## 💬 Contact & Discussion

Want to chat, give feedback, or share your ideas about DCTelnet?

You can reach me on [Retro 32 BBS](https://www.retro32.com/retro32-bbs), where I go by *Bruno*,
or join the discussion on the [EAB forum thread](https://eab.abime.net/showthread.php?t=121939)


---

## ✨ Features

- **68000** compatible
- **ZModem** upload & download using XPR libraries
- Fast and efficient design : DCTelnet access the TCP/IP stack directly
- Flexible display modes: fullscreen, windowed on Workbench or minimizable/iconifiable
- Address book with username & password entries
- Additional terminal emulations via **XEM libraries**
- User-definable screen modes & fonts
- **multiple simultaneous** Telnet connections
- **No** MUI, ClassAct or ReAction required
- **No** `telser.device` required

### New in 2.0-UP/X (unofficial release)

**Display**
- **Built-in terminal renderer** (default on 68020 and better): ANSI, VT and
  xterm sequences, **UTF-8**, **256-colour and RGB text**, box and block
  characters, window titles and scrollback (Shift+Page Up/Down or the mouse
  wheel). The ibmcon.device and console.device renderers stay available
  (Display > Renderer).
- **RTG and 256-colour AGA** screen modes (Picasso96, CyberGraphX); the 16 ANSI
  colours get their own pens, also on the Workbench
- **Palette editor** for the 16 ANSI colours, with live preview
- Screen font and palette changes apply in place, without closing the screen
- **80 and 132 columns**; the BBS is told the real text area (NAWS) after every
  resize or font change
- **Modern Icons** tool bar, a grey interface on DCTelnet's own screen, and the
  TopazPro font for square-pixel screen modes

**Connections**
- **SSH** through ssh.library, with builds for 68000, 68020, 68040 and 68060
  (the Installer picks the right one) and known host keys
- **Rlogin**, with automatic login on Synchronet
- **Redial, keep alive and connect timeout**

**BBS**
- **PETSCII** for Commodore 64 BBSes: C64 characters built in (no font to
  install), whole-screen case switch, Ctrl and Commodore colour keys
- **ANSI music** by the GW-BASIC PLAY rules, as in SyncTERM
- **iCE Colours** and a Terminal Type choice: Automatic, BBS, Unix or PETSCII
- Cursor position requests get an answer, so BBS animations and countdowns no
  longer stall
- **Per-BBS settings**: each Address Book entry can override the Screen,
  Terminal, Keyboard and Transfer settings and has a comment and a login
  macro; settings changed during a connection can be saved back to the entry
- Import a **SyncTERM phone book** into the Address Book

**Everyday use**
- **Mouse**: select and copy text, paste, mouse reports to the host, bracketed
  paste and the NewMouse wheel
- **Capture to File**, Save Screen, and find text in the Scroll Back (F2)
- **ARexx port** (DCTELNET.1) with CONNECT, SEND, WAITFOR and more; the login
  macro can wait for a text
- Home, End, Page Up, Page Down and Insert keys
- Bell as a flash, a sound or off
- Settings from 1.x versions load with everything kept

---

## ⚙️ Requirements

- Kickstart / Workbench **v2.00 (V36)** or higher
- A TCP/IP stack: **AmiTCP**, **Roadshow**, **Miami**, UAE bsdsocket.library or compatible

---

## 📦 Installation

1. Download the DCTelnet package either from Aminet:
   [comm/tcp/DCTelnet](https://aminet.net/package/comm/tcp/DCTelnet)
   or from the [GitHub releases section](https://github.com/bruno-frederic/dctelnet/releases).

2. The DCTelnet archive includes an **Installer** script.
   When available, using the *Installer* tool is the recommended installation method.

---

### Manual installation

On older AmigaOS versions where the *Installer* tool is not present (for example, pre-3.1.4 systems), DCTelnet can be installed **manually**. In this case, copy the required files to their respective system directories:

- `DCTelnet/Devs/ibmcon.device` -> `DEVS:`
- Libraries from `DCTelnet/Libs` -> `LIBS:`
- Fonts from `DCTelnet/Fonts` -> `FONTS:`

On AmigaOS / Kickstart **2.00 & 2.02**, the default versions of
reqtools.library and xprzmodem.library fail to load.
Replace them with the following compatible versions:
- `DCTelnet/Libs-r36/reqtools.library` -> `LIBS:`
- `DCTelnet/Libs-r36/xprzmodem.library` -> `LIBS:`

---

## 📖 How to use

Please refer to the included **DCTelnet.guide** documentation for detailed
usage instructions.

It is in **AmigaGuide** format and can be opened directly on AmigaOS 3.*

On Amiga OS 2.*, the AmigaGuide tool is required (available on Aminet: [text/hyper/aguide34](https://aminet.net/package/text/hyper/aguide34)). In this case, update the "Default Tool" of the DCTelnet.guide icon to: SYS:Utilities/AmigaGuide

---

## 🐞 Known Bugs


### Character display issues and font selection

If some characters appear incorrect or garbled after connecting to a BBS, the issue is usually related to the selected terminal font. Make sure the font matches the character set expected by the remote system.

Many PC-based BBSes require an **IBM Code Page 437 (CP437)** font to display line-drawing characters and ANSI artwork correctly. In such cases, select one of the bundled IBM/ANSI fonts: *HyperANSI*, *IBM*, *P0T-IBM*, or *ANSEY*.

On **Amiga-based BBSes**, graphics are often designed for the classic **Topaz** font. This is the case, for example, on *Retro 32 BBS*.


### Rendering glitches and compatibility issues on several BBSes

Rendering issues occur on multiple BBSes, whether using ibmcon.device or
XemVT340.library for display. Some screens freeze, misalign the UI, or fail
to display animations.

Improved with ibmcon.device 1.9: DCTelnet now answers the BBS's cursor
position requests (Device Status Report), Cursor Backward and Forward stop
at the edge of the line, and CTerm 24-bit colour sequences are ignored
instead of moving the cursor. Not yet confirmed on every BBS in the report.

Details on [GitHub issue #11](https://github.com/bruno-frederic/dctelnet/issues/11).


### Network issues with AmiBerry and FS-UAE on Linux

Connection issues have been reported when using the integrated TCP/IP stack provided by AmiBerry or FS-UAE under Linux. The problem appears to be specific to these emulators' bsdsocket.library implementations. Using a different IP stack resolves the issue.

Details on [GitHub issue #13](https://github.com/bruno-frederic/dctelnet/issues/13).


---

## 🛠️ Build environment

**As of September 2026:**

- **IDE**: VS Code (on Windows)
- Docker is used for the official build and can run a telnetd container for testing.
- **Build instructions**: See [CONTRIBUTING.md](CONTRIBUTING.md) for the official CI build procedure and contributor requirements.
- **Alternative build toolchains** (outside the official CI build):
  - **VBCC** on Windows, using my custom toolchain:
  [vbcc-bin](https://github.com/bruno-frederic/vbcc-bin/tree/Bruno_toolchain)
  - Bebbo's Amiga-GCC 6.5.0b
  - SAS/C v6.58 on AmigaOS 3.2 can build all DCTelnet 1.x versions, but not 2.0,
    which uses C99 features unsupported by SAS/C.
- Icon Editor : IconEdit from AmigaOS 3.2.3

### Source code

All source code referenced below, including **DCTelnet 1.5 / 1.6** and its
required third-party libraries, is publicly available on **Aminet**.

- Based on the **DCTelnet 1.5 / 1.6** sources:
  [comm/tcp/dct16_src](https://aminet.net/package/comm/tcp/dct16_src)

The required third-party source code dependencies are also included:

- **ReqTools 2.9** from [util/libs/ReqToolsDev](https://aminet.net/package/util/libs/ReqToolsDev)
- **XPRZModem 3.1** library and sources from "comm/misc/xprz31.lha" package on Aminet CD 2
- **Xem 2.0** from [comm/term/XEM2_0](https://aminet.net/package/comm/term/XEM2_0)

---

## 🙏 Credits

- **DCTelnet** was originally written by **Zed**.
- new 68000-compatible build by **Bruno FREDERIC**
- 2.0-UP/X additions by [**spotUP**](https://github.com/spotUP):
  - the built-in terminal renderer (ANSI, VT, xterm, UTF-8, 256 and 24-bit
    colours, 132 columns, scrollback, mouse reports, bracketed paste), built on
    the vtcon engine from [UP-Term](https://github.com/spotUP/UP-Term)
  - PETSCII Mode for Commodore 64 BBSes, with the Petscii and PetsciiLower fonts
  - SSH-2 connections and ssh.library for every CPU (68000 to 68060), and Rlogin
  - ANSI music and the bell options
  - text selection, copy and paste, capture, ANSI screen save and scrollback search
  - per-BBS settings, SyncTERM phone book import, redial, keep alive and the
    login macro
  - the ARexx port
  - RTG and 256-colour support, the palette editor, Modern Icons and the
    own-screen interface
  - ibmcon.device 1.5-1.11
- The PETSCII key and fallback tables are from SyncTERM by Rob Swindell (LGPL).
- VBCC Docker builder image by [**George Sokianos**](https://github.com/walkero-gr)
- ReqTools is Copyright © Nico François and Magnus Holmgren

---

### ⚖️ License

The original **DCTelnet** code by Zed was released without an explicit license and remains *all rights reserved*. See [LICENSE file](LICENSE) for details.

This repository provides the source for preservation and maintenance only.
