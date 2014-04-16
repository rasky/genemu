Genesis / MegaDrive Emulator
============================

This emulator is being written just for fun and for teaching to friends how to write
game emulators. It is not meant to ever grow user-friendly features for now,
and can just be used as a source code reference on how emulators works. It focuses
only on accuracy (not performance).


What's emulated
===============
 * All the basics: CPUs, sprites, planes, DMA, etc.
 * Accurate Z80 RESET/BUSREQ process
 * Wide resolution change (32 vs 40 cells)
 * FM sound (YM2612)
 * Basic HINTERRUPT emulation

Todo list
=========
The following is a rough list of missing features or things to be fixed:

 * Shadow / highlight mode
 * Correct sprite priority vs bkg
 * Correct hinterrupt / hv counter
 * SN7689 audio chip
 * Vertical cell scrolling through VSRAM
 * PAL vs NTSC game detection
 * Window horizontal clipping (and the infamous "window bug")
 * Accurate timing for DMA
