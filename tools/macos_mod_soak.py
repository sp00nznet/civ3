#!/usr/bin/env python3
"""Bounded single-player Civ-Content compatibility test; removes only QA copies."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from macos_qa_cleanup import remove_clones
from macos_assets import fresh_settings
from macos_smoke import assess
from macos_roundtrip import BAD, digest, turns

ROOT=Path(__file__).resolve().parents[1]
CASES={
    "cheaper-upgrades":(427, "Cheaper Upgrades.biq"),
    "no-civ-traits":(516, "No Civ Traits.biq"),
    "no-unique-units":(546, "No Unique Units.biq"),
    "quick-civ":(577, "Quick Civ.biq"),
    "with-plague":(607, "With Plague.biq"),
    "age-of-bronze":(637, "age of bronze.bix"),
}
p=argparse.ArgumentParser()
p.add_argument("case",choices=CASES)
p.add_argument("--output",type=Path,required=True)
p.add_argument("--play",type=int,default=155)
p.add_argument("--interval",type=int,default=1200)
a=p.parse_args()
source=(ROOT/"game").resolve()
dest=a.output.resolve()
if dest.exists() or dest.is_relative_to(source) or source.is_relative_to(dest):
    p.error("Output must be new and distinct from original game")
if not 300<=a.interval<=6000 or not 60<=a.play<=360:
    p.error("Invalid duration/interval")
click_y, filename=CASES[a.case]
matches=list(source.rglob(filename))
if not matches:p.error(f"Required mod file absent: {filename}")
original_ini=source/"Conquests/conquests.ini"
original_hash=digest(original_ini)
mod_hash=digest(matches[0])
dest.mkdir(parents=True)
report={"case":a.case,"mod_file":filename,"mod_sha256":mod_hash,"result":"not-run",
        "game_binary_sha256":digest(ROOT/"build-macos/civ3")}
try:
    game=dest/"private-game"
    subprocess.run(["/bin/cp","-cR",str(source),str(game)],check=True)
    saves=game/"Conquests/Saves"
    if saves.exists():saves.rename(dest/"inherited-saves")
    saves.mkdir()
    fresh_settings(game,dest)
    commands=[str(ROOT/"build-macos/civ3"),"--headless","--run","--nosteam",
        "--watchdog",str(a.play+90),"--play",str(a.play),
        "--game",str(game),"--work",str(ROOT/"work"),
        "--exe",str(ROOT/"work/Civ3Conquests.exe"),"--shot",str(dest/"final.ppm"),
        "--move","240,509@4","--open","240,509@6",
        "--click",f"200,{click_y}@20",
        "--click","972,744@24","--click","969,740@40",
        "--autopilot",f"{a.interval}@90"]
    print("Testing",a.case,"menu y=",click_y,"file=",filename,flush=True)
    with (dest/"run.log").open("w") as out:
        process=subprocess.run(commands,cwd=ROOT,stdout=out,stderr=subprocess.STDOUT,
                               timeout=a.play+130,env=dict(os.environ,HLE_FILETRACE="1"))
    text=(dest/"run.log").read_text(errors="replace")
    failures=assess(process.returncode,text,dest/"final.ppm")
    failures += [f"runtime: {m}" for m in BAD if m in text]
    end=turns(text)[1]
    if end is None or end<1:failures.append("no completed game turn")
    saves_created=sorted((saves/"Auto").glob("*.SAV"))
    if not saves_created:failures.append("no fresh autosave")
    if f'CreateFileA "Scenarios\\{filename}'.lower() not in text.lower() and f'CreateFileA "..\\Civ3PTW\\Scenarios\\{filename}'.lower() not in text.lower():
        failures.append("selected mod file was not explicitly opened")
    if digest(original_ini)!=original_hash or digest(matches[0])!=mod_hash:
        failures.append("source game preferences or mod changed")
    report.update(result="pass" if not failures else "fail",exit=process.returncode,
                  final_turn=end,autosaves=[x.name for x in saves_created],
                  errors=failures,log="run.log",framebuffer="final.ppm")
    print(json.dumps(report,indent=2),flush=True)
finally:
    report["clones_removed"]=remove_clones(dest,["private-game","inherited-saves"])
    (dest/"result.json").write_text(json.dumps(report,indent=2)+"\n")
if report["result"]!="pass":raise SystemExit(1)
