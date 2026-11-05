#!/usr/bin/env python3
"""Isolated bounded single-player soak. No source game writes."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
from datetime import datetime, timezone

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from macos_roundtrip import private_game, digest, turns, BAD
from macos_smoke import assess
from macos_qa_cleanup import remove_clones
from macos_rss import run_with_rss, summarize_rss

ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument("--seed-save", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--binary", type=Path, default=ROOT/"build-macos/civ3",
                    help="Native executable to test without replacing the playable build")
parser.add_argument("--play", type=int, default=180)
parser.add_argument("--interval", type=int, default=950)
parser.add_argument("--rss-sample", action="store_true",
                    help="Periodically sample this game's resident memory (RSS)")
args=parser.parse_args()
source=(ROOT/"game").resolve()
seed=args.seed_save.resolve()
output=args.output.resolve()
binary=args.binary.expanduser().resolve()
if not binary.is_file():
    parser.error("Built native executable required")
if not seed.is_file() or not seed.is_relative_to(source):
    parser.error("Seed must be an existing local game save")
if output.exists() or output.is_relative_to(source) or source.is_relative_to(output):
    parser.error("Output must be new, separate from live game")
if not 300<=args.interval<=8000 or not 30<=args.play<=600:
    parser.error("Invalid duration or autopilot interval")
output.mkdir(parents=True)
ini=source/"Conquests/conquests.ini"
ini_digest=digest(ini) if ini.exists() else None
seed_digest=digest(seed)
report={"seed":str(seed),"source_sha256":seed_digest,"result":"not-run","binary_sha256":digest(binary)}
clone=None
try:
    clone=private_game(source,output/"private-game",seed,seed.name)
    shot=output/"final.ppm"
    log=output/"run.log"
    command=[str(binary),"--headless","--run","--watchdog",str(args.play+120),
             "--play",str(args.play),"--game",str(clone),"--work",str(ROOT/"work"),
             "--exe",str(ROOT/"work/Civ3Conquests.exe"),"--shot",str(shot),
             "--move","240,425@1","--open","240,425@2","--click","621,557@10",
             "--wait","0xA74EA4@11","--key","0x0D@14","--autopilot",f"{args.interval}@20"]
    print("SOAK command args:",command,flush=True)
    if args.rss_sample:
        returncode, samples = run_with_rss(command, ROOT, log, args.play + 160)
        report["memory"] = summarize_rss(samples)
        (output/"rss-samples.json").write_text(json.dumps(samples, indent=2)+"\n")
    else:
        with log.open("w") as stream:
            result=subprocess.run(command,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,
                                  timeout=args.play+160)
        returncode = result.returncode
    content=log.read_text(errors="replace")
    loaded,finish=turns(content)
    failures=assess(returncode,content,shot)
    failures += [f"Runtime: {x}" for x in BAD if x in content]
    if loaded is None or loaded<1:failures.append("load milestone absent")
    if finish is None or loaded is not None and finish<loaded+5:failures.append("fewer than 5 completed turns")
    autos=sorted((clone/"Conquests/Saves/Auto").glob("*.SAV"))
    if not autos:failures.append("no autosaves produced")
    if digest(seed)!=seed_digest or (digest(ini) if ini.exists() else None)!=ini_digest:
        failures.append("source game save/preferences changed")
    report.update(result="pass" if not failures else "fail",loaded_turn=loaded,final_turn=finish,
                  exit=returncode,failures=failures,autosave_count=len(autos),
                  autosave_names=[p.name for p in autos],shot=shot.name,log=log.name)
    print(json.dumps(report,indent=2),flush=True)
finally:
    if clone is not None:
        try: report["temporary_copies_removed"]=remove_clones(output,["private-game","private-game-inherited-saves"])
        except Exception as ex: report["cleanup_error"]=repr(ex)
    (output/"result.json").write_text(json.dumps(report,indent=2)+"\n")
if report["result"]!="pass":raise SystemExit(1)
