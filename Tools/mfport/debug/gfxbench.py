#!/usr/bin/env python3
"""Graphics options: frame time and picture per setting at fixed cameras.

    python3 Tools/mfport/debug/gfxbench.py <out dir> [scene ...]     # run (4.5 min a scene)
    python3 Tools/mfport/debug/gfxbench.py <out dir> --res=3840x2160  # four times the pixels: a weak GPU's load
    python3 Tools/mfport/debug/gfxbench.py <out dir> --reverse        # the settings in the opposite order
    python3 Tools/mfport/debug/gfxbench.py <out dir> --gpu=intel      # on the integrated GPU (Vulkan ICD chosen)
    python3 Tools/mfport/debug/gfxbench.py <out dir> --android        # on the phone adb sees (an Android
                                                                      # Development package installed)
    python3 Tools/mfport/debug/gfxbench.py <out dir> --report        # table of what was run

One game process per scene: every 9 game seconds it sets the scalability groups to Medium plus
one change, takes a screenshot (`DFShot`) and 300 frames of `csvprofile`, uncapped
(`t.MaxFPS 0`), rendering off screen at 1920 x 1080 (`--res`). The scenes are community maps. Medium runs first and last: the two
differ by the run's own drift. The table gives median frame, game thread, render thread and GPU ms, the change against
Medium and how far the picture moves from Medium's (mean grey levels of 255, HUD rows cut).
Run it on mains power with nothing else busy: on battery the GPU is throttled.

A laptop also slows itself down when hot, so each scene starts cooled (`--cooldown GPU_C CPU_C`,
60 65 by default; `--no-cooldown`), and the conditions are written down with the numbers: mains
or battery, power profile, governor, and every 2 s the GPU's and the CPU package's temperature,
the GPU's clock and power, the CPU's mean clock, and whether the driver holds the GPU back
(thermal slowdown, power cap). The report prints their range per scene and warns
when a scene started warm, ran on battery or was slowed down. Each row carries the sample
nearest its capture, so a slow row can be told from a hot one. The machine warms up through a
scene whatever it started at (20 - 30 C in 4.5 minutes here), so the last rows run hotter than
the first: the two Medium rows show by how much, and a second run with `--reverse` averaged
with the first cancels it.

`--android` runs the same script on a phone: the command line goes into the game's
UECommandLine.txt, the game is started and left to quit by itself, the CSVs and screenshots come
back over adb. The phone draws at its own resolution (render scale 100 %) and frame pace, and is
cooled before each scene by its own sensors (`dumpsys thermalservice`: the GPU's G3D at most
48 C, the skin 38 C, thermal status 0); those sensors and the battery are logged every 5 s."""
import csv
import glob
import json
import os
import shutil
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[3]
GPU = 'auto'   # --gpu=intel
OUT = Path.cwd()
BASE = {'sg.AntiAliasingQuality': 1, 'sg.TextureQuality': 1, 'sg.ViewDistanceQuality': 1, 'sg.EffectsQuality': 1,
        'sg.PostProcessQuality': 1, 'sg.ShadowQuality': 1, 'sg.GlobalIlluminationQuality': 1, 'sg.ReflectionQuality': 1,
        'sg.ShadingQuality': 1, 'sg.FoliageQuality': 1, 'r.ScreenPercentage': 100,
        'r.MipMapLODBias': 0}   # the menu's half size ("Blurry") is its own setting, over the texture level
CONFIGS = [('medium', {}), ('TextureBlurry', {'sg.TextureQuality': 0, 'r.MipMapLODBias': 1})]
for group, levels in (('AntiAliasing', (0, 2)), ('Texture', (0, 2, 3, 4)), ('ViewDistance', (0, 2, 3)), ('Effects', (0, 3)),
                      ('PostProcess', (0, 3)), ('Shadow', (0, 3)), ('GlobalIllumination', (0, 3)), ('Reflection', (0, 3)),
                      ('Shading', (0, 3))):
    for lv in levels:
        CONFIGS.append((f'{group}{lv}', {f'sg.{group}Quality': lv}))
for pct in (50, 75, 150):
    CONFIGS.append((f'scale{pct}', {'r.ScreenPercentage': pct}))
CONFIGS.append(('low', {k: 0 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('high', {k: 2 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('epic', {k: 3 for k in BASE if k.startswith('sg.')}))
CONFIGS.append(('medium_again', {}))
SCENES = {
    'sardine_roof': ('mf-Sardine_City', 'DFCam 20000 9000 1500 200 -35'),
    'sardine_street': ('mf-Sardine_City', 'DFCam 18423 7400 -380 90 8'),
    'facility_hall': ('MF-Test_Facility', 'DFCam 9076 3600 -60 180 0'),   # indoors, lamps
}
STEP = 9.0   # game seconds per setting
ADB = os.environ.get('ADB', str(Path.home() / 'Android' / 'Sdk' / 'platform-tools' / 'adb'))
PACKAGE = 'com.fistodul.desktopforces'
PHONE_GAME = f'/sdcard/Android/data/{PACKAGE}/files/UnrealGame/DesktopForces'   # UECommandLine.txt goes here
PHONE_SAVED = PHONE_GAME + '/DesktopForces/Saved'
FRAMES = 300


def _read(path: str) -> str | None:
    try:
        return Path(path).read_text().strip()
    except OSError:
        return None


def _nvidia(fields: str) -> list[str]:
    try:
        out = subprocess.run(['nvidia-smi', f'--query-gpu={fields}', '--format=csv,noheader,nounits'], capture_output=True, text=True, timeout=5)
        return [v.strip() for v in out.stdout.strip().split(',')] if out.returncode == 0 and out.stdout.strip() else []
    except (OSError, subprocess.TimeoutExpired):
        return []


def _number(text: str | None) -> float | None:
    try:
        return float(text)
    except (TypeError, ValueError):
        return None


def sample() -> dict:
    """Temperatures [C], clocks [MHz] and power [W] now; None for what this machine doesn't tell."""
    reasons = ('sw_power_cap', 'hw_thermal_slowdown', 'sw_thermal_slowdown', 'hw_power_brake_slowdown')
    gpu = _nvidia('temperature.gpu,power.draw,clocks.gr,utilization.gpu,' + ','.join(f'clocks_event_reasons.{r}' for r in reasons)) + [None] * 8
    cpu = next((_number(_read(f'{z}/temp')) for z in sorted(glob.glob('/sys/class/thermal/thermal_zone*'))
                if _read(f'{z}/type') == 'x86_pkg_temp'), None)
    clocks = [c for c in (_number(_read(f)) for f in glob.glob('/sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq')) if c]
    igpu = next((_number(_read(f)) for f in glob.glob('/sys/class/drm/card*/gt_cur_freq_mhz')), None)   # Intel i915: shares the CPU's heat
    mains = [_read(f) for f in glob.glob('/sys/class/power_supply/A*/online')]
    return {'t': time.time(), 'gpu_c': _number(gpu[0]), 'gpu_w': _number(gpu[1]), 'gpu_mhz': _number(gpu[2]), 'gpu_util': _number(gpu[3]),
            'cpu_c': cpu / 1000 if cpu else None, 'cpu_mhz': round(statistics.mean(clocks) / 1000) if clocks else None,
            'igpu_mhz': igpu, 'mains': ('1' in mains) if mains else None, 'load1': round(os.getloadavg()[0], 2),
            'slowed': [r for r, state in zip(reasons, gpu[4:8]) if state == 'Active']}   # the driver holding the GPU back


def conditions() -> dict:
    """What decides a laptop's speed besides the game: written into every scene's results."""
    def tool(*args):
        try:
            return subprocess.run(args, capture_output=True, text=True, timeout=5).stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            return None
    monitor = next((line.strip() for line in (tool('xset', 'q') or '').splitlines() if 'Monitor is' in line), None)
    return {'power_profile': tool('powerprofilesctl', 'get'), 'governor': _read('/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor'),
            'no_turbo': _read('/sys/devices/system/cpu/intel_pstate/no_turbo'), 'battery': [_read(f) for f in glob.glob('/sys/class/power_supply/BAT*/status')],
            'gpu': _nvidia('name,pstate,power.limit'), 'monitor': monitor, 'now': sample()}


def cool_down(gpu_max: float, cpu_max: float, timeout: float = 1500.0) -> dict:
    """Wait until GPU and CPU are this cool, so a scene's first rows aren't its fastest."""
    start = time.monotonic()
    while True:
        now = sample()
        cool = (now['gpu_c'] is None or now['gpu_c'] <= gpu_max) and (now['cpu_c'] is None or now['cpu_c'] <= cpu_max)
        if cool or time.monotonic() - start > timeout:
            return {'waited_s': round(time.monotonic() - start), 'gpu_c': now['gpu_c'], 'cpu_c': now['cpu_c'], 'reached': cool, 'limits_c': [gpu_max, cpu_max]}
        time.sleep(10)


def adb(*args: str, timeout: float = 60.0) -> str:
    try:
        return subprocess.run([ADB, *args], capture_output=True, text=True, timeout=timeout).stdout
    except (OSError, subprocess.TimeoutExpired):
        return ''


def phone_sample() -> dict:
    """The phone's sensors (C) and battery now."""
    out = {'t': time.time()}
    live = False   # dumpsys lists cached temperatures (the last throttling event's) before the live ones
    for line in adb('shell', 'dumpsys', 'thermalservice', timeout=15).splitlines():
        line = line.strip()
        if line.startswith('Thermal Status:'):
            out['status'] = _number(line.split(':')[1])
        elif line.endswith(':') and not line.startswith('Temperature{'):
            live = line.startswith('Current temperatures from HAL')
        elif live and line.startswith('Temperature{') and 'mName=' in line:
            fields = dict(f.split('=', 1) for f in line[12:-1].split(', ') if '=' in f)
            names = {'G3D': 'gpu_c', 'BIG': 'cpu_c', 'VIRTUAL-SKIN': 'skin_c', 'battery': 'battery_c'}
            if fields.get('mName') in names:
                out.setdefault(names[fields['mName']], _number(fields.get('mValue')))
    battery = adb('shell', 'dumpsys', 'battery', timeout=15)
    for key, name in (('level', 'battery_pct'), ('status', 'battery_status')):
        out[name] = next((_number(l.split(':')[1]) for l in battery.splitlines() if l.strip().startswith(key + ':')), None)
    return out


def phone_cool_down(gpu_max: float = 40.0, skin_max: float = 34.0, timeout: float = 1500.0) -> dict:
    start = time.monotonic()
    while True:
        now = phone_sample()
        cool = now.get('status', 0) == 0 and (now.get('gpu_c') or 0) <= gpu_max and (now.get('skin_c') or 0) <= skin_max
        if cool or time.monotonic() - start > timeout:
            return {'waited_s': round(time.monotonic() - start), 'gpu_c': now.get('gpu_c'), 'skin_c': now.get('skin_c'), 'reached': cool}
        time.sleep(10)


def script(scene: str, res: tuple[int, int] | None) -> tuple[str, list[str], float]:
    """The scene's map and its -DFExec lines; the game seconds they take."""
    game_map, cam = SCENES[scene]
    # r.SetRes: the saved window size wins over -ResX / -ResY (a phone has its own)
    cmds = ['@3:DFLoadout'] + ([f'@3.5:r.SetRes {res[0]}x{res[1]}w'] if res else []) + ['@4:t.MaxFPS 0', '@4:r.VSync 0', f'@5:{cam}']
    t = 8.0
    for name, change in CONFIGS:
        for k, v in {**BASE, **change}.items():
            cmds.append(f'@{t:.1f}:{k} {v}')
        cmds.append(f'@{t + 2.5:.1f}:DFShot bench_{scene}_{name}')
        cmds.append(f'@{t + 3.5:.1f}:csvprofile frames={FRAMES}')
        t += STEP
    return game_map, cmds, t


def read_rows(files: list, samples: list, stamp) -> dict:
    """Median frame / thread / GPU ms per setting from its CSV; the conditions nearest its capture."""
    rows = {}
    for (name, _), f in zip(CONFIGS, files):
        with open(f, newline='') as fh:
            data = list(csv.reader(fh))
        if not data:   # a capture the game quit on
            continue
        head = data[0]
        cols = {c: i for i, c in enumerate(head)}
        vals = {c: [] for c in ('FrameTime', 'GameThreadTime', 'RenderThreadTime', 'GPUTime', 'RHIThreadTime') if c in cols}
        for r in data[1:]:
            if len(r) < len(head) or r[0] == head[0]:
                continue
            try:
                for c in vals:
                    vals[c].append(float(r[cols[c]]))
            except ValueError:
                continue
        rows[name] = {c: round(statistics.median(v), 3) for c, v in vals.items() if v}
        rows[name]['frames'] = len(next(iter(vals.values()), []))
        if samples:
            at = min(samples, key=lambda smp: abs(smp['t'] - stamp(f)))
            rows[name].update({k: v for k, v in at.items() if k != 't'})
    return rows


def run_android(scene: str):
    """The scene on the phone: see the module's notes."""
    game_map, cmds, t = script(scene, None)
    cooled = phone_cool_down()
    adb('shell', 'am', 'force-stop', PACKAGE)
    adb('shell', 'rm', '-rf', PHONE_SAVED + '/Profiling/CSV', PHONE_SAVED + '/Screenshots/Android')
    line = f'../../../DesktopForces/DesktopForces.uproject /Game/Maps/{game_map}?game=tdm?Bots=0 -nosound -DFExec="{"|".join(cmds)}" -DFQuitAfter={t + 15:.0f}'   # its last capture written
    cmdline = OUT / 'UECommandLine.txt'
    cmdline.write_text(line + '\n')
    adb('push', str(cmdline), PHONE_GAME + '/UECommandLine.txt')
    # a dark screen pauses the game: stay on while it runs, then the owner's setting back (svc power
    # stayon false would clear a "Stay awake" they set in the developer options)
    stay = adb('shell', 'settings', 'get', 'global', 'stay_on_while_plugged_in').strip() or '0'
    adb('shell', 'svc', 'power', 'stayon', 'true')
    # the panel's top rate: with Smooth Display off (60) the pacer can't go past 60 and every frame
    # reads 16.7 ms; 120 while the game runs, then the owner's setting back
    peak = adb('shell', 'settings', 'get', 'system', 'peak_refresh_rate').strip()
    adb('shell', 'settings', 'put', 'system', 'peak_refresh_rate', '120.0')
    samples, start = [], time.time()
    try:
        adb('shell', 'monkey', '-p', PACKAGE, '-c', 'android.intent.category.LAUNCHER', '1')
        time.sleep(20)
        while time.time() - start < t + 300 and adb('shell', 'pidof', PACKAGE).strip():
            samples.append(phone_sample())
            time.sleep(5)
    finally:
        adb('shell', 'am', 'force-stop', PACKAGE)
        adb('shell', 'rm', '-f', PHONE_GAME + '/UECommandLine.txt')
        adb('shell', 'settings', 'put', 'global', 'stay_on_while_plugged_in', stay if stay.isdigit() else '0')
        if _number(peak) is not None:
            adb('shell', 'settings', 'put', 'system', 'peak_refresh_rate', peak)
        else:
            adb('shell', 'settings', 'delete', 'system', 'peak_refresh_rate')
    local = OUT / f'{scene}.phone'
    shutil.rmtree(local, ignore_errors=True)
    local.mkdir()
    for remote in ('Profiling/CSV', 'Screenshots/Android', 'Logs/DesktopForces.log'):
        adb('pull', f'{PHONE_SAVED}/{remote}', str(local), timeout=300)
    shutil.copy(local / 'DesktopForces.log', OUT / f'{scene}.log') if (local / 'DesktopForces.log').exists() else None
    files = sorted((local / 'CSV').glob('*.csv'))   # named by time: the order they were taken
    stamps = {f: start + (i + 0.5) * (t - 8.0) / max(1, len(files)) + 8.0 for i, f in enumerate(files)}
    rows = read_rows(files, samples, lambda f: stamps[f])
    dst = OUT / scene
    dst.mkdir(exist_ok=True)
    for name, _ in CONFIGS:
        shot = local / 'Android' / f'bench_{scene}_{name}.png'
        if shot.exists():
            shutil.move(str(shot), dst / f'{name}.png')
    def span(key):
        vals = [smp[key] for smp in samples if smp.get(key) is not None]
        return [min(vals), max(vals)] if vals else None
    shot = dst / 'medium.png'
    setting = lambda ns, key: adb('shell', 'settings', 'get', ns, key).strip()
    state = {'cool_down': cooled, 'peak_refresh_rate': peak, 'phone': adb('shell', 'getprop', 'ro.product.model').strip(), 'samples': len(samples),
             'brightness': setting('system', 'screen_brightness'), 'auto_brightness': setting('system', 'screen_brightness_mode'),
             'power_saver': setting('global', 'low_power'), 'screen_off_ms': setting('system', 'screen_off_timeout'),
             'range': {k: span(k) for k in ('gpu_c', 'cpu_c', 'skin_c', 'battery_c', 'status', 'battery_pct')},
             'size': list(Image.open(shot).size) if shot.exists() else None}
    (OUT / f'{scene}.json').write_text(json.dumps({'files': len(files), 'configs': len(CONFIGS), 'size': state['size'], 'phone': state, 'rows': rows}, indent=1))
    (OUT / f'{scene}.samples.json').write_text(json.dumps(samples))
    print(scene, 'csv files', len(files), 'of', len(CONFIGS), state['range'])


def run(scene: str, res=(1920, 1080), cooldown: tuple[float, float] | None = (60.0, 65.0)):
    global OUT
    game_map, cmds, t = script(scene, res)
    csv_dir = REPO / 'Saved' / 'Profiling' / 'CSV'
    shots = REPO / 'Saved' / 'Screenshots' / 'LinuxEditor'
    for f in glob.glob(str(csv_dir / '*.csv')):
        os.remove(f)
    log = OUT / f'{scene}.log'
    cooled = cool_down(*cooldown) if cooldown else None
    before, samples, stop = conditions(), [], threading.Event()

    def watch():
        while not stop.wait(2.0):
            samples.append(sample())
    watcher = threading.Thread(target=watch, daemon=True)
    watcher.start()
    env = dict(os.environ)
    if GPU == 'intel':   # only the Intel driver visible: the engine can't pick the NVIDIA GPU
        env['VK_ICD_FILENAMES'] = env['VK_DRIVER_FILES'] = '/usr/share/vulkan/icd.d/intel_icd.json'
    args = ['python3', str(REPO / 'Tools' / 'play.py'), game_map, 'tdm', '0', '-RenderOffscreen', '-windowed',
            f'-ResX={res[0]}', f'-ResY={res[1]}', '-nosound', '-unattended', '-DFExec=' + '|'.join(cmds),
            f'-DFQuitAfter={t + 2:.0f}', f'-abslog={log}']
    subprocess.run(args, cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=1500, env=env)
    gpu_name = next((line.split('Name:', 1)[1].strip() for line in log.read_text(errors='ignore').splitlines() if 'LogRHI:' in line and 'Name:' in line), None)
    stop.set()
    watcher.join()
    files = sorted(glob.glob(str(csv_dir / '*.csv')), key=os.path.getmtime)
    rows = read_rows(files, samples, os.path.getmtime)
    dst = OUT / scene
    dst.mkdir(exist_ok=True)
    for name, _ in CONFIGS:
        s = shots / f'bench_{scene}_{name}.png'
        if s.exists():
            shutil.move(str(s), dst / f'{name}.png')
    shot = dst / 'medium.png'
    size = list(Image.open(shot).size) if shot.exists() else None
    if size != list(res):
        print(f'! {scene}: rendered at {size}, not {list(res)}')
    def span(key):
        vals = [smp[key] for smp in samples if smp[key] is not None]
        return [min(vals), max(vals)] if vals else None
    state = {'cool_down': cooled, 'before': before, 'gpu_name': gpu_name, 'range': {k: span(k) for k in ('gpu_c', 'cpu_c', 'gpu_mhz', 'igpu_mhz', 'cpu_mhz', 'gpu_w', 'gpu_util', 'load1')},
             'on_mains_throughout': all(smp['mains'] is not False for smp in samples) and before['now']['mains'] is not False,
             'slowed_samples': {r: n for r in ('sw_power_cap', 'hw_thermal_slowdown', 'sw_thermal_slowdown', 'hw_power_brake_slowdown')
                                if (n := sum(r in smp['slowed'] for smp in samples))}, 'samples': len(samples)}
    (OUT / f'{scene}.json').write_text(json.dumps({'files': len(files), 'configs': len(CONFIGS), 'size': size, 'conditions': state, 'rows': rows}, indent=1))
    (OUT / f'{scene}.samples.json').write_text(json.dumps(samples))
    print(scene, 'csv files', len(files), 'of', len(CONFIGS))
    for name, _ in CONFIGS:
        print(f'  {name:22s}', rows.get(name))


def diff(a: Path, b: Path) -> float:
    if not (a.exists() and b.exists()):
        return float('nan')
    x = np.asarray(Image.open(a).convert('RGB'), np.float32)[80:-100]   # without the HUD rows
    y = np.asarray(Image.open(b).convert('RGB'), np.float32)[80:-100]
    return float(np.abs(x - y).mean())


def report():
    scenes = [p.stem for p in sorted(OUT.glob('*.json')) if not p.name.endswith('.samples.json')]
    whole = {s: json.loads((OUT / f'{s}.json').read_text()) for s in scenes}
    data = {s: whole[s]['rows'] for s in scenes}
    for s in scenes:
        c = whole[s].get('conditions')
        if whole[s].get('phone'):
            ph = whole[s]['phone']
            print(f"{s} on {ph['phone']}: {ph['samples']} samples, cooled {ph['cool_down']}; during {ph['range']}; {ph['size']}")
        if not c:
            continue
        b, r, cd = c['before'], c['range'], c['cool_down']
        print(f"{s} on {c.get('gpu_name')}: {b['power_profile']} profile, governor {b['governor']}, {'mains' if c['on_mains_throughout'] else 'BATTERY'}, {b['monitor'] or 'no X display'}; "
              f"started at GPU {b['now']['gpu_c']} C / CPU {b['now']['cpu_c']} C" + (f" after {cd['waited_s']} s of cooling" if cd else ', not cooled'))
        print(f"  during: NVIDIA GPU {r['gpu_c']} C, {r['gpu_mhz']} MHz, {r['gpu_w']} W, {r['gpu_util']} % busy; Intel GPU {r.get('igpu_mhz')} MHz; CPU {r['cpu_c']} C, {r['cpu_mhz']} MHz mean; load {r['load1']}")
        warn = [w for w, bad in (('started warm', bool(cd) and not cd['reached']), ('ran on battery', not c['on_mains_throughout']),
                                 (f"GPU held back in {c['slowed_samples']} of {c['samples']} samples", bool(c['slowed_samples'])),
                                 ('CPU near its limit', bool(r['cpu_c']) and r['cpu_c'][1] >= 95)) if bad]
        if warn:
            print('  ! ' + '; '.join(warn))
    names = list(next(iter(data.values())))
    print('setting'.ljust(22) + ''.join(f'{s[:16]:>34s}' for s in scenes))
    print(' ' * 22 + ''.join(f'{"frame":>7s}{"game":>6s}{"rend":>6s}{"gpu":>6s}{"%med":>5s}{"pic":>4s}' for _ in scenes))
    for n in names:
        line = n.ljust(22)
        for s in scenes:
            r, m = data[s].get(n, {}), data[s]['medium']
            if not r:
                line += ' ' * 34
                continue
            ft, gpu = r.get('FrameTime', 0), r.get('GPUTime', 0)
            line += f'{ft:7.2f}{r.get("GameThreadTime", 0):6.2f}{r.get("RenderThreadTime", 0):6.2f}{gpu:6.2f}{(ft / m["FrameTime"] - 1) * 100:+5.0f}{diff(OUT / s / f"{n}.png", OUT / s / "medium.png"):4.1f}'
        print(line)


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    OUT = Path(sys.argv[1]).resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    if '--report' in sys.argv:
        report()
    else:
        size = next((tuple(int(n) for n in a[6:].split('x')) for a in sys.argv if a.startswith('--res=')), (1920, 1080))
        GPU = next((a[6:] for a in sys.argv if a.startswith('--gpu=')), 'auto')
        if '--reverse' in sys.argv:
            CONFIGS.reverse()
        cool = None if '--no-cooldown' in sys.argv else (60.0, 65.0)
        if '--cooldown' in sys.argv:
            at = sys.argv.index('--cooldown')
            cool = (float(sys.argv[at + 1]), float(sys.argv[at + 2]))
            del sys.argv[at:at + 3]
        for s in [a for a in sys.argv[2:] if not a.startswith('--')] or SCENES:
            run_android(s) if '--android' in sys.argv else run(s, size, cool)
        report()
