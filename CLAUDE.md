# xemu — Original Xbox Emulator (QEMU fork)

xemu is a QEMU-based emulator for the original Microsoft Xbox console. The
repo is structured like upstream QEMU plus an `hw/xbox/` tree containing the
NV2A GPU, MCPX southbridge, USB-OHCI, IDE, and the rest of the Xbox-specific
hardware emulation. Most editing for Xbox-specific work happens under
`hw/xbox/`.

Upstream docs: <https://xemu.app>. The in-repo `README.md` is essentially
empty — for human-facing setup go to <https://xemu.app/docs/dev/>.

## Why this checkout exists (current investigation context)

There is a sister project at **`/mnt/g/dev/halo`** — `halo-re/halo`, an
incremental decompilation/re-implementation of *Halo: Combat Evolved* (debug
XBE `cachebeta.xbe`, build 2276, MD5 `c7869590a1c64ad034e49a5ee0c02465`).
That project re-implements C functions and patches them back into the
original XBE. To verify re-implementations behave like the originals at
runtime, the patched XBE has to actually boot inside xemu — and right now
**cachebeta.xbe does not boot in xemu**. It silently hangs at a black screen
after the BIOS splash.

This checkout is here to investigate that hang and (eventually) fix it
upstream. **The whole reason this CLAUDE.md exists is to make it cheap for a
fresh Claude session to pick up the investigation without re-running the
dead ends.**

Read `/mnt/g/dev/halo/CLAUDE.md` for the halo project's conventions.

## What we already know about the cachebeta hang

A previous session sampled the running emulator extensively via the QEMU
monitor and pinned down a lot:

### The "spin" is the kernel idle loop, not an NV2A bug

When you sample `info registers` while cachebeta is hung, **30 out of 30
samples** land at one of two adjacent EIPs in xboxkrnl:

```
8002039e:  fb            sti
8002039f:  90            nop          ← sampled here
800203a0:  90            nop          ← sampled here
800203a1:  fa            cli
800203a2:  3b 6d 00      cmp ebp, [ebp+0]   ; classic empty-LIST_ENTRY check
800203a5:  74 0c         je  800203b3
800203a7:  b1 02         mov cl, 2
800203a9:  e8 8e 8e ff ff  call 0x8001923c   ; clears bit 2 of *0x8003f70c
```

`EBP = 0x8003f91c` is a doubly-linked list head. The loop body is the
textbook Windows kernel `KiIdleLoop` pattern (`STI; NOP; NOP; CLI` to allow
interrupts to fire in a tiny window, then check whether any work landed).
The 30/30 sample density means **no other thread is ever ready to run**.

`EAX = 0xfd000140` (NV2A `NV_PMC_INTR_EN_0`) and `ECX = 0xfd601300` (NV2A
PRMCIO/legacy VGA CRTC) in those samples are **stale leftovers** from some
earlier MMIO access — the idle loop itself does not touch them. **Anchoring
on these registers leads down a wrong rabbit hole; we already did that.**

### The title is loaded but its main thread is blocked

`info mem` (kernel CR3) shows the Halo XBE has been demand-loaded into the
following user-space ranges only:

```
0x00010000-0x00016000  -rw    ~24K   (XBE header + early data)
0x00016000-0x00017000  -r-     4K
0x00017000-0x00018000  -rw     4K
0x00018000-0x00039000  -r-   132K   (early .text)
0x00039000-0x0003b000  -rw     8K
```

That's it. **132 KB of `.text` and headers**, then the title's main thread
gets blocked on something in the kernel and never demand-faults more pages.
The lowest function in halo's `kb.json` is `ai_initialize` at `0x3f670` —
**17 KB past where the loader stopped**, so none of the cataloged Halo
functions have executed. In particular, `game_initialize` at the top of
`game.c` (and everything it calls — `telnet_console_initialize` at
`0x130980`, `game_engine_initialize`, etc.) has **not** run yet.

The block is therefore in the very early CRT init / pre-`main` code of the
title XBE.

### What we have ruled out

| Hypothesis | Verdict | How |
|---|---|---|
| NV2A spin polling PMC/PRMCIO regs | ❌ false | Disasm shows it's `KiIdleLoop`, the GPU registers are stale |
| `XNetStartup` waiting on network init | ❌ false | Set `[net] enable = false` in `xemu.toml`, hang is identical at the same EIP |
| Missing assets on the ISO | ❌ false | The ISO is 2.4 GB and includes the full prototype `maps/`, `bink/`, `debug.txt`, etc. |
| pfifo crash like Halo Beta 1749 (xemu issue #631) | ❌ false | Different failure mode — 1749 *crashes* with a `pfifo_run_pusher` assert; cachebeta runs cleanly with **zero** assertions, CPU resets, or unimplemented-op messages from `-d int,cpu_reset,guest_errors,unimp` |

### Strongest live hypothesis

Cachebeta is the **debug "beta" build**. The classic reason debug Xbox dev
builds hang at startup is **waiting for development tooling** (Xbox Debug
Monitor / xbdm, profiler agents, or Halo's own dev-tools backend) **to
connect**. cachebeta has many strings around `network_game_*`,
`telnet_console`, `bypass_security`, `XNET_STARTUP_BYPASS_SECURITY [ON]`,
`profile.txt`, etc. that `cache.xbe` (also 2276 but not debug-beta) does not
have — and `cache.xbe` boots fine in xemu, while cachebeta does not.

But: `telnet_console_initialize` is at `0x130980`, which is well past the
loaded range, so it has not run yet. Whatever blocks the title fires
**before** `telnet_console_initialize`. The candidate code is in the title's
CRT/early-`main` path that's calling some kernel API (likely `NtCreateFile`
on a magic device path, `IoOpenSymbolicLinkObject`, an `Nt*` event wait, or
a synchronous debug-print into `HalDisplayString`/`KdReportFatalError` that
expects a debugger to consume it).

**This hypothesis is unverified.** The next session should test it.

## Hardware emulation files of interest

Already inspected and ruled out (or at least understood):

- `hw/xbox/nv2a/pmc.c` — PMC register handler. `NV_PMC_BOOT_0` returns
  `0x02A000A3`, `INTR_0` and `INTR_EN_0` work normally. Anything else
  returns `0`. **Not the cause of the hang.**
- `hw/xbox/nv2a/prmcio.c` — PRMCIO delegates straight to QEMU's
  `vga_ioport_read` / `vga_ioport_write`. **Not the cause of the hang.**
- `hw/xbox/nv2a/nv2a_regs.h` — register offset definitions. `0x140` =
  `NV_PMC_INTR_EN_0`.
- `hw/xbox/mcpx/nvnet/nvnet.c` — Xbox NIC. Networking is **not** the
  problem (verified by disabling it).

Worth investigating next:

- `hw/xbox/xbox.c` — board init. PCI device wiring, RAM size, BIOS load.
- `hw/xbox/smbus*.c` — SMBus / PIC16. Some titles probe SMBus at startup.
- `hw/xbox/eeprom*.c` — EEPROM emulation. The dev-mode flags live in
  EEPROM and a wrong value here could cause a debug build to wait forever
  for tooling.
- `hw/xbox/mcpx/lpc.c` — LPC bridge / SuperIO. Serial port and the legacy
  debug console live here. **High-priority** because cachebeta is a debug
  build and debug builds love spamming the LPC serial port.
- `hw/usb/hcd-ohci.c` — USB. Title startup may enumerate controllers; if
  it never gets a "controller present" event it could spin in input init.
- xboxkrnl is **not** in this repo — it's in the BIOS image the user
  supplies. Symbol info has to come from elsewhere (Cxbx-Reloaded,
  OpenXbox, or a community xboxkrnl PDB).

## Building xemu

### Windows cross-compile (primary method)

The native WSL build environment is broken (`meson` venv path issues), so
use the Docker-based Windows cross-compile from the **parent directory**:

```bash
cd /mnt/g/dev
docker run --rm -v $PWD/xemu:/xemu -w /xemu \
    -e CCACHE_DIR=/xemu/ccache \
    ghcr.io/xemu-project/xemu-win64-toolchain:latest \
    ./build.sh -p win64-cross
```

Output: **`build/qemu-system-i386.exe`** (Windows PE binary). Run it on the
Windows side directly.

### Native Linux build (currently broken in this WSL env)

```bash
cd /mnt/g/dev/xemu
./build.sh                 # release, ~5-10 min on a fast machine
./build.sh --debug         # debug build with DWARF symbols
```

`build.sh` runs `./configure --target-list=i386-softmmu --extra-cflags=-DXBOX=1`
then `make`. Output: **`build/qemu-system-i386`**. That binary is the Linux
equivalent of the Windows `xemu.exe` and is invoked the same way.

In WSL with WSLg, the Linux build can render to the Windows desktop
directly — no need to cross-compile to Windows for iterative testing.

## Test loop for cachebeta hang investigation

The halo project ships tooling that talks to xemu via the QEMU HMP monitor.
**Use it.** Don't reinvent it. After building xemu:

1. Edit `/mnt/g/dev/halo/tools/xemu.env`:
   ```
   XEMU_BIN="/mnt/g/dev/xemu/build/qemu-system-i386"
   XEMU_TOML="/mnt/c/Users/<you>/AppData/Roaming/xemu/xemu/xemu.toml"
   ```
   (Or whatever path xemu writes to on Linux —
   `~/.local/share/xemu/xemu/xemu.toml`. Check `xemu_settings_get_base_path`
   in xemu's stdout on first run.)

2. Launch with the monitor enabled:
   ```bash
   /mnt/g/dev/halo/tools/xemu.sh -m "/mnt/h/Console/Microsoft Xbox/Halo 2276 beta.iso"
   ```

3. From another shell, sample CPU state:
   ```bash
   /mnt/g/dev/halo/tools/xemu-mon.py "info registers"
   /mnt/g/dev/halo/tools/xemu-mon.py "x /16wx 0x80020380"
   /mnt/g/dev/halo/tools/xemu-mon.py "info mem"
   /mnt/g/dev/halo/tools/xemu-mon.py "info tlb"
   /mnt/g/dev/halo/tools/xemu-mon.py --repeat 30 --interval 0.05 "info registers"
   /mnt/g/dev/halo/tools/xemu-mon.py --quit "info status"     # cleanly stops xemu
   ```

   Useful HMP commands inside the monitor: `info registers`, `info mem`,
   `info tlb`, `x /Nwx ADDR` (virtual), `xp /Nwx ADDR` (physical),
   `info irq`, `info pic`, `info chardev`, `info pci`, `info qtree`.

4. **A successful fix is unambiguous**: the EIP samples will move out of
   `0x800203a0`/`0x8002039f` and start hitting addresses below `0x80000000`
   (user-space title code). `info mem` will show the user-space mapping
   growing past `0x3b000`.

5. The halo project has a `boot_hash.sh` oracle harness ready
   (`tools/boot_hash.sh`) that becomes immediately useful the moment
   cachebeta gets past its current block — it dumps and hashes a memory
   region after a fixed wait, intended for re-implementation bisection.

## Concrete next investigations (in priority order)

1. **Find what kernel API the title is blocking on.** The most direct path:
   walk the kernel's thread list. The structure at `0x8003f8cc` (the spin
   loop's `lea ebx, [0x8003f8cc]`) is in xboxkrnl's `.data` and looks like
   a per-CPU control block; `+0x50` is a list head. With xboxkrnl symbols
   we'd know its name immediately. Without symbols, dump it as a struct
   and try to match it to known NT-style KPCR/KPRCB layouts from
   ReactOS/Cxbx-Reloaded. The non-idle thread's `WaitBlocks` will tell us
   what object it's waiting on.

2. **Add logging to the LPC serial port.** Beta debug builds spew debug
   output here. If xemu drops the data silently, we'd never see it. Wire
   the serial output to xemu's stdout and re-run cachebeta — the last few
   lines before the hang are likely diagnostic.

3. **Audit `hw/xbox/eeprom*` for dev-mode flags.** Real Xbox dev kits had a
   different EEPROM personality. If cachebeta reads the EEPROM and finds a
   "retail" kit, it might wait forever for non-existent tooling (or vice
   versa). The eeprom file path is in `xemu.toml` `[sys.files] eeprom_path`.

4. **Look for a "no debugger present, halt" trap early in cachebeta.** In
   the running emulator, set up a script to sample EIP at very small
   intervals starting from xemu launch (before the kernel even reaches
   idle). If we see EIPs in user space briefly *before* the title gets
   blocked, those are the addresses to disassemble.

5. **Bring up the GDB stub** (currently blocked by a port collision: xemu's
   NAT in `xemu.toml` already binds `0.0.0.0:1234`, which is QEMU's default
   gdbstub port). Either disable that NAT forward in `xemu.toml` for the
   debugging session, or modify `tools/xemu.sh` in the halo project to
   pass `-gdb tcp::1235` instead of `-s`. With the gdb stub, we can set
   hardware watchpoints on `0x8003f91c` (the wait list head) and catch
   anything that ever inserts a thread into it.

6. **File an upstream issue at <https://github.com/xemu-project/xemu/issues>**
   summarizing the diagnosis. Reference issue #631 (Halo Beta 1749 — same
   game, different failure mode) but make clear this is a *separate* bug:
   no asserts, idle hang, debug build only.

## Things NOT to do (avoid dead ends from past sessions)

- **Don't anchor on `0xfd000140` and `0xfd601300` registers.** Those are
  stale EAX/ECX values from a prior call. The kernel's idle loop does not
  read them. Adding handlers for them in `pmc.c`/`prmcio.c` will not unstick
  cachebeta.
- **Don't assume it's a network init issue.** Confirmed false by disabling
  networking entirely.
- **Don't assume it's missing game assets.** The ISO contains the full
  prototype folder.
- **Don't try to test against `cachebeta.xbe`'s sibling builds** unless you
  understand what they are: `2276defaultP.xbe` (= `final.xbe`, release) and
  `2276P.xbe` (= `cache.xbe`, debug-without-beta) **both boot fine** in
  xemu. Only `2276betaP.xbe` (= `cachebeta.xbe`) hangs. Use that asymmetry
  — diff cachebeta against cache.xbe to find what extra code path is in
  the broken build. The halo project already has `/tmp/cachebeta-strs.txt`
  and `/tmp/cache-strs.txt` from `strings` output for this purpose.
