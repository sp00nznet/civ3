#!/usr/bin/env python3
"""Run one bundled Conquests scenario with only isolated, removable game assets."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from macos_qa_cleanup import remove_clones
from macos_assets import fresh_settings
from macos_smoke import assess
from macos_roundtrip import BAD,digest,turns
ROOT=Path(__file__).resolve().parents[1]
CASES={"mesopotamia":(0,"1 Mesopotamia.biq"),"rise-of-rome":(1,"2 Rise of Rome.biq"),
       "fall-of-rome":(2,"3 Fall of Rome.biq"),"middle-ages":(3,"4 Middle Ages.biq"),
       "mesoamerica":(4,"5 Mesoamerica.biq"),"age-of-discovery":(5,"6 Age of Discovery.biq"),
       "sengoku":(6,"7 Sengoku - Sword of the Shogun.biq"),
       "napoleonic":(7,"8 Napoleonic Europe.biq"),"wwii-pacific":(8,"9 WWII in the Pacific.biq"),
       "intro-ancient-treasures":(9,"Intro1 Ancient Treasures.biq"),
       "intro-three-sisters":(10,"Intro2 The Three Sisters.biq"),
       "intro-new-alliances":(11,"Intro3 New Alliances.biq")}
ap=argparse.ArgumentParser()
ap.add_argument("case",choices=CASES)
ap.add_argument("--output",type=Path,required=True)
ap.add_argument("--play",type=int,default=140)
ap.add_argument("--interval",type=int,default=1200)
a=ap.parse_args()
source=(ROOT/"game").resolve()
target=a.output.resolve()
if target.exists() or target.is_relative_to(source) or source.is_relative_to(target):ap.error("Output must be new and outside source game")
if not 300<=a.interval<=6000 or not 70<=a.play<=360:ap.error("Invalid duration or interval")
index,biq=CASES[a.case]
assert (source/"Conquests/Conquests"/biq).is_file()
target.mkdir(parents=True)
binary=ROOT/"build-macos/civ3"
source_ini=source/"Conquests/conquests.ini"
source_hash=digest(source_ini)
run_result={"case":a.case,"expected_scenario":biq,"result":"not-run","game_binary_sha256":digest(binary)}
try:
    clone=target/"private-game"
    subprocess.run(["/bin/cp","-cR",str(source),str(clone)],check=True)
    saves=clone/"Conquests/Saves"
    if saves.exists():saves.rename(target/"inherited-saves")
    saves.mkdir()
    fresh_settings(clone,target)
    commands=[str(binary),"--headless","--run","--nosteam","--watchdog",str(a.play+90),
        "--play",str(a.play),"--game",str(clone),"--work",str(ROOT/"work"),
        "--exe",str(ROOT/"work/Civ3Conquests.exe"),"--shot",str(target/"final.ppm"),
        "--move","240,467@4","--open","240,467@6",
        "--click",f"200,{127+30*index}@20","--click","972,744@24",
        "--click","969,740@40",
        "--autopilot",f"{a.interval}@90"]
    with (target/"run.log").open("w") as stream:
        env=dict(os.environ,HLE_FILETRACE="1")
        process=subprocess.run(commands,cwd=ROOT,stdout=stream,stderr=subprocess.STDOUT,
                               timeout=a.play+120,env=env)
    log=(target/"run.log").read_text(errors="replace")
    screenshots=target/"final.ppm"
    failures=assess(process.returncode,log,screenshots)
    failures += [f"runtime error: {m}" for m in BAD if m in log]
    # Every scenario menu launch must be separately verified; no false pass on
    # simply showing the menu. Require a turn and a fresh autosave.
    final=turns(log)[1]
    if final is None or final <=0:failures.append("no playable game turn")
    autos=sorted((saves/"Auto").glob("*.SAV"))
    if not autos:failures.append("no scenario autosave")
    # File tracing should include the exact BIQ or an asset path from its mod folder.
    scenario_recorded=biq.lower() in log.lower()
    if not scenario_recorded:failures.append("scenario BIQ was not explicitly observed opening")
    if source_hash!=digest(source_ini):failures.append("original source preferences were modified")
    run_result.update(result="pass" if not failures else "fail",code=process.returncode,turn=final,
        scenario_file_observed=scenario_recorded,autosaves=[p.name for p in autos],
        failures=failures,log="run.log",screenshot="final.ppm")
    print(json.dumps(run_result,indent=2),flush=True)
finally:
    try:run_result["clones_removed"]=remove_clones(target,["private-game","inherited-saves"])
    except Exception as e:run_result["cleanup_error"]=str(e)
    (target/"result.json").write_text(json.dumps(run_result,indent=2)+"\n")
if run_result["result"]!="pass":raise SystemExit(1)
