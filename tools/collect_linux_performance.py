#!/usr/bin/env python3
"""Read-only Linux CPU/GPU/thermal baseline. Run on the phone; keep output private."""
import glob,json
from pathlib import Path
result={}
patterns=[
'/sys/devices/system/cpu/online',
'/sys/devices/system/cpu/cpufreq/policy*/affected_cpus',
'/sys/devices/system/cpu/cpufreq/policy*/scaling_*',
'/sys/devices/system/cpu/cpufreq/policy*/cpuinfo_*',
'/sys/class/devfreq/*/name','/sys/class/devfreq/*/cur_freq',
'/sys/class/devfreq/*/available_frequencies','/sys/class/devfreq/*/governor',
'/sys/class/thermal/thermal_zone*/type','/sys/class/thermal/thermal_zone*/temp',
'/sys/kernel/debug/clk/clk_summary',
'/sys/kernel/debug/pm_genpd/pm_genpd_summary',
]
for pattern in patterns:
    for filename in sorted(glob.glob(pattern)):
        p=Path(filename)
        if not p.is_file():continue
        try:result[filename]=p.read_text().strip()
        except OSError as e:result[filename]={'error':str(e)}
print(json.dumps(result,indent=2))
