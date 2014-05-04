#!/usr/bin/env python
import subprocess
import json
import multiprocessing

GENEMU = "../build/genemu"

TESTSUITE = [
    ("Titan Overdrive MegaDemo (PAL)", "../debugroms/titan-overdrivemegademo-v1.bin", "PAL",  range(30*50, 6*60*50, 5*50)),
    ("Titan Overdrive MegaDemo (NTSC)", "../debugroms/titan-overdrivemegademo-v1.bin", "NTSC", range(30*60, 6*60*60, 5*60)),
]

def runtest((name, rom, mode, sshots)):
    cmds = [ GENEMU ]
    cmds.append("--mode")
    cmds.append(mode)
    cmds.append("--screenshots")
    cmds.append(",".join(map(str, sshots)))
    cmds.append(rom)

    subprocess.check_call(cmds, stdout=open("/dev/null"))

def main():
    games = []
    for name,rom,mode,sshots in TESTSUITE:
        g = {}
        g["name"] = name
        g["sshots"] = [ "%s.%d.%s.bmp" % (rom,ss,mode) for ss in sshots ]
        games.append(g)

    json.dump({"games":games}, open("testout.json", "w"))

    p = multiprocessing.Pool()
    p.map_async(runtest, TESTSUITE).get(999999)


if __name__ == "__main__":
    main()
