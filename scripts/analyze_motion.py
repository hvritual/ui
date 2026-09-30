#!/usr/bin/env python3
"""Summarize recorded present samples; never infer LCD FPS from idle time."""
from __future__ import annotations
import argparse
import csv
import hashlib
import json
from pathlib import Path

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def quantiles(values: list[int]) -> dict:
    if not values:
        return {"count": 0}
    ordered = sorted(values)
    def at(q: float) -> float:
        index = (len(ordered)-1)*q
        low = int(index)
        return (ordered[low]+(ordered[min(low+1,len(ordered)-1)]-ordered[low])*(index-low))/1e6
    return {"count":len(values),"unit":"ms","p50":at(.50),"p95":at(.95),
            "p99":at(.99),"max":ordered[-1]/1e6}

def analyze(directory: Path) -> dict:
    timeline = directory/'timeline.csv'
    report = json.loads((directory/'report.json').read_text())
    samples = []
    required = ['presents','present_complete_ns','present_dragging','present_settling',
                'update_duration_ns','render_duration_ns','present_duration_ns','present_scroll_x']
    previous = None
    count = 0
    with timeline.open(newline='') as stream:
        for row in csv.DictReader(stream):
            count += 1
            current = {k:int(row[k]) for k in required}
            if any(current[k]<0 for k in required if k!='present_scroll_x'):
                raise ValueError('negative timing/counter')
            if current['present_dragging'] not in (0,1) or current['present_settling'] not in (0,1):
                raise ValueError('invalid motion flag')
            if previous and current['presents']<previous['presents']:
                raise ValueError('present counter reversed')
            if previous and current['presents']==previous['presents']:
                if current!=previous:
                    raise ValueError('a repeated present has different stored measurements')
                continue
            if previous and current['present_complete_ns']<=previous['present_complete_ns']:
                raise ValueError('present clock reversed')
            samples.append(current)
            previous = current
    if not samples or samples[-1]['presents']>report['presents']:
        raise ValueError('timeline/report mismatch')
    motion=[r for r in samples if r['present_dragging'] or r['present_settling']]
    return {
        'schema':1,'source_commit':report['commit'],
        'files':{n:digest(directory/n) for n in ('timeline.csv','report.json')},
        'scope':'CPU-side recorded stage durations, not LCD scanout or photon latency',
        'rows':count,'unique_present_samples':len(samples),
        'reported_presents':report['presents'],
        'unrecorded_final_presents':report['presents']-samples[-1]['presents'],
        'motion_samples':len(motion),
        'motion_stage_timings':{k:quantiles([r[k] for r in motion]) for k in required[4:7]},
        'mixed_session':{k:report[k] for k in ('wall_seconds','cpu_percent_one_core','peak_rss_kib')},
        'limitations':[
            'The final forced cleanup present is normally outside timeline.csv.',
            'Repeated presents carry the same stored timings and are counted once.',
            'The b0d8cdd update duration excludes input-callback work and preceding skipped ticks.',
            'Mixed-session CPU and total-present-count / duration are not active-motion CPU or FPS.',
            'No human smoothness, orientation, media or input-fault acceptance is generated.'
        ]
    }

def main() -> int:
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    print(json.dumps(analyze(args.directory),indent=2,ensure_ascii=False,sort_keys=True))
    return 0

if __name__=='__main__':
    try:
        raise SystemExit(main())
    except (OSError,ValueError,KeyError,TypeError) as exc:
        raise SystemExit('MOTION_ANALYSIS_FAILED: '+str(exc))
