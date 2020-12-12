# clairvoyance
![Build status](https://github.com/0vercl0k/clairvoyance/workflows/Builds/badge.svg)

Clairvoyance (/**klɛərˈvɔɪəns**/; from French clair meaning *clear* and voyance meaning *vision*) from [Wikipedia](https://en.wikipedia.org/wiki/Clairvoyance).

## Overview

**clairvoyance** creates a colorful visualization of the page properties of an entire 64-bit process address space (user and kernel) running on a Windows 64-bit kernel.

To transform the 1 dimension space that is the address space into a 2 dimensions visualization, the [hilbert space-filling curve](https://en.wikipedia.org/wiki/Hilbert_curve) is used. Each colored pixel on the picture represents the page properties (*UserRead*, *UserReadWrite*, etc.) of a 4KB page.

The address space is directly generated by manually parsing the [four-level]() page tables hierarchy associated with a process from a kernel crash-dump that has been generated using [WindDbg](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/debugger-download-tools).

The picture is generated using a simple ASCII-based format called [Portable Pixel Map or PPM](https://en.wikipedia.org/wiki/Netpbm#File_formats) which is very easy to generate without taking extra dependencies. A bunch of converters, viewers, etc. exist on the Internet.

Compiled binaries are available in the [releases](https://github.com/0vercl0k/clairvoyance/releases) section.

Shouts out to:
- [Alexandru Radocea](https://twitter.com/defendtheworld) and [Georg Wicherski](https://twitter.com/ochsff) for the inspiration (see their BlackHat USA 2013 research: *[Visualizing Page Tables for Exploitation](https://media.blackhat.com/us-13/US-13-Wicherski-Hacking-like-in-the-Movies-Visualizing-Page-Tables-WP.pdf)*),
- [The Hacker's delight second edition]()'s chapter 16 *Hilbert's curve* for providing the algorithms used in clairvoyance.

## Usage

## Build

The [CI](https://github.com/0vercl0k/clairvoyance/blob/main/.github/workflows/clairvoyance.yml) builds clairvoyance on Linux using [clang++-11](https://clang.llvm.org/) and on Windows using Microsoft's [Visual studio 2019](https://visualstudio.microsoft.com/vs/community/).

To build it yourself you can use the scripts in [build/](https://github.com/0vercl0k/clairvoyance/blob/main/build):

```
(base) clairvoyance\build>build-msvc.bat
(base) clairvoyance\build>cmake ..
-- Selecting Windows SDK version 10.0.19041.0 to target Windows 10.0.19042.
-- Configuring done
-- Generating done
-- Build files have been written to: clairvoyance/build

(base) clairvoyance\build>cmake --build . --config RelWithDebInfo
Microsoft (R) Build Engine version 16.8.2+25e4d540b for .NET Framework
Copyright (C) Microsoft Corporation. All rights reserved.

  clairvoyance.vcxproj -> clairvoyance\build\RelWithDebInfo\clairvoyance.exe
  Building Custom Rule clairvoyance/CMakeLists.txt
```

## Various findings

The below are things I've noticed on a kernel crash-dump generated from an Hyper-V VM of Windows:

```
kd> vertarget
Windows 10 Kernel Version 18362 UP Free x64
Product: WinNt, suite: TerminalServer SingleUserTS
Edition build lab: 18362.1.amd64fre.19h1_release.190318-1202
Machine Name:
Kernel base = 0xfffff805`36800000 PsLoadedModuleList = 0xfffff805`36c432f0
Debug session time: Sat Jul 25 10:00:19.637 2020 (UTC - 8:00)
System Uptime: 0 days 0:18:53.609
```

### Type of pages

Windows doesn't seem to be using huge pages (1GB) or at least I have not seen one being used in any of the dumps I collected.

Large pages are used in abundance to map some kernel executables like the Windows kernel *nt* for example:

```
kd> ? nt
Evaluate expression: -8773703827456 = fffff805`36800000
```

```
VA:0xfffff80536800000, PA:0x2400000 (KernelReadWriteExec, Large, PML4E:0xd5745f80, PDPTE:0x42080a0, PDE:0x4209da0, PTE:0x0)
```

### Virtual address sinks

A bunch of large kernel memory sections are mapped against the same physical page (filled with zero):

```
VA:0xffffc27ef4401000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef4402000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef4403000, PA:0x4200000 (KernelRead, Normal, ...)
...
VA:0xffffc27ef63fb000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef63fc000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef63fd000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef63fe000, PA:0x4200000 (KernelRead, Normal, ...)
VA:0xffffc27ef63ff000, PA:0x4200000 (KernelRead, Normal, ...)
```

Here is smaller one (the region is not completely contiguous, there are a few holes):

```
VA:0xffffc27ed2201000, PA:0x4300000 (KernelRead, Normal, ...)
VA:0xffffc27ed2202000, PA:0x4300000 (KernelRead, Normal, ...)
VA:0xffffc27ed2203000, PA:0x4300000 (KernelRead, Normal, ...)
...
VA:0xffffc27ed25fc000, PA:0x4300000 (KernelRead, Normal, ...)
VA:0xffffc27ed25fd000, PA:0x4300000 (KernelRead, Normal, ...)
VA:0xffffc27ed25fe000, PA:0x4300000 (KernelRead, Normal, ...)
VA:0xffffc27ed25ff000, PA:0x4300000 (KernelRead, Normal, ...)
```

## Authors

Axel '[0vercl0k](https://twitter.com/0vercl0k)' Souchet
