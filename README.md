Genesis / MegaDrive Emulator
============================

This emulator is being written just for fun and for teaching to friends how to write
game emulators. It is not meant to ever grow user-friendly features for now,
and can just be used as a source code reference on how emulators works. It focuses
only on accuracy (not performance).

How to use
==========

   $ genemu romfile

Genemu supports both bin and smd romfiles. Use --help for some additional
command line option.


What's emulated
===============
 * All the basics: CPUs, sprites, planes, DMA, etc.
 * PAL vs NTSC timings 
 * Accurate Z80 RESET/BUSREQ process
 * Correct hinterrupt / hv counter
 * Correct sprite priority vs bkg
 * Correct sprite overflow and masking
 * Wide resolution change (32 vs 40 cells)
 * Shadow / highlight mode
 * FM sound (YM2612)
 * Basic HINTERRUPT emulation
 * Vertical cell scrolling through VSRAM
 * Window horizontal clipping

Todo list
=========
The following is a rough list of missing features or things to be fixed:

 * Interlace mode
 * SN7689 audio chip
 * Accurate timing for DMA
 * The infamous "window bug"

Branches
========
The branch "precise_dma_fifo" implements sub-scanline timing of hblank and
tries to emulate precise DMA and FIFO timings, down to full access slot
emulations, but without going down to a per-cycle emulation. It is quite
tricky to get everything right, but the branch is almost to a zero regressions
with a few improvements with advanced effects. A few things that work better
in the branch:

 * DMA effects timed with HBLANK seems to work better (eg: 512-color pictures
   used in demos). In fact, I think the master code can get to the same level
   of accuracy with a few hacks on timing (to be verified).
 * Direct Color mode used in demos. This still requires a hack since we still
   do per-scanline VDP rendering, but the DMA is almost perfectly synchronized
   with the emulation; the images still appear off-center and partly off-screen,
   but I'm still surprised they kind of work.

