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
