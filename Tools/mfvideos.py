#!/usr/bin/env python3
"""Mobile Forces gameplay videos from the community channel, as reference for lighting and look.

  python3 Tools/mfvideos.py scan            # list the channel, read titles and opening frames
  python3 Tools/mfvideos.py report          # what was found, per map
  python3 Tools/mfvideos.py download        # whole videos: the newest of each map (--all: every one)
  python3 Tools/mfvideos.py frames --map mf-warehouse --every 30
                                            # stills, from a download or straight from the stream

Most titles name the map and mode. Where one doesn't, scan keeps the video's first seconds (the
channel's title card and the server's welcome text name them) and runs OCR on them; it only fetches what isn't in the manifest yet,
so it can be stopped and run again. Everything goes to Tools/mfport/work/videos (ignored by git):
manifest.json, manifest.csv, intro/<id>.mp4, full/<map>/<date>_<id>.mp4 (download),
frames/<map>/<id>_<seconds>.jpg. A whole video is about 1.3 GB an hour at 720p60.

A wrong guess is fixed in overrides.json next to the manifest: {"<video id>": {"map": "mf-Polar",
"mode": "ctf"}}. Needs ffmpeg; yt-dlp and RapidOCR go into a venv in the work folder by themselves.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CHANNEL = "https://www.youtube.com/@mobileforcesstillalive/videos"
INTRO_SECONDS = 30
# where to look in the opening seconds: the title card fades in around 3-5 s, the welcome text and
# the "<mode> in <map>" line under the HUD stay a while
OCR_TIMES = (2, 4, 6, 9, 13, 20)
PIP_NAMES = {"yt_dlp": "yt-dlp", "rapidocr_onnxruntime": "rapidocr_onnxruntime"}  # module -> pip package

# GameModeClassAliases in Config/DefaultEngine.ini, and what videos call them; longest first wins
MODES = {
    "tdm": ("team deathmatch", "team death match", "team dm", "teamdm", "tdm"),
    "dm": ("deathmatch", "death match", "dm"),
    "ctf": ("capture the flag", "ctf"),
    "holdout": ("holdout", "hold out"),
    "sc": ("safecracker", "safe cracker", "safecrack"),
    "det": ("detonation",),
    "trailer": ("trailer",),
    "cap": ("captains", "captain"),
    "koh": ("king of the hill", "koth"),
    "arena": ("arena",),
    "br": ("battle royale",),
    "coop": ("coop", "co-op", "obstacle course"),
    "moball": ("moball", "mo ball"),
    "tug": ("tug-o-war", "tug o war", "tug of war", "tugowar"),
    "zombie": ("zombie",),
    # mods the port doesn't have (yet): kept so the video still says what it shows
    "race": ("mental race", "mentalrace", "race"),
    "domination": ("domination",),
    "hideseek": ("hide&seek", "hide and seek", "hide & seek"),
    "nakedgun": ("naked gun",),
    "freeplay": ("freeplay", "free play"),
}
# what the channel calls a map whose file is named otherwise (Polish names, initials)
MAP_ALIASES = {"taiga": "tajga", "shambles": "pobojowisko", "newfieldcity": "nc", "spaceshuttle": "shuttlelaunch"}
MODE_ALIASES = sorted(((a, m) for m, names in MODES.items() for a in names), key=lambda x: -len(x[0]))


# --- names -------------------------------------------------------------------------------------

def map_key(file_name: str) -> str:
    """mf-Sardine_City -> sardinecity, MF-BloodWarzV1 -> bloodwarz, COOP-CoopOneUpdate -> coopone."""
    s = file_name.lower()
    s = re.sub(r"^(mfs?|dm|mb|coop|br|zm|ctf)[-_]", "", s)
    s = re.sub(r"([-_]?kf|[-_]?ver[-_]?i+|vs?\d+|updated?|\d+)$", "", s)
    return re.sub(r"[^a-z0-9]", "", s)


def squash(text: str) -> str:
    return re.sub(r"[^a-z0-9]", "", text.lower())


def known_maps(root: Path = ROOT) -> dict[str, list[str]]:
    """Normalised name -> map files (pack/name), from every .umap the project has."""
    maps: dict[str, list[str]] = {}
    for umap in sorted(list(root.glob("Plugins/*/Content/**/*.umap")) + list(root.glob("Content/**/*.umap"))):
        rel = umap.relative_to(root).parts
        pack = rel[1] if rel[0] == "Plugins" else "Game"
        if umap.stem.startswith("MF_Test"):
            continue
        maps.setdefault(map_key(umap.stem), []).append(f"{pack}/{umap.stem}")
    return maps


def find_mode(text: str) -> str | None:
    low = " " + re.sub(r"\s+", " ", text.lower()) + " "
    for alias, mode in MODE_ALIASES:
        if re.search(r"(?<![a-z])" + re.escape(alias) + r"(?![a-z])", low):
            return mode
    return None


def find_map(text: str, maps: dict[str, list[str]]) -> tuple[str | None, str]:
    """The map a title or OCR text names: whole name, then 'the' dropped, then the start of one
    ("Holiday" for HolidayTown). Returns (key, how)."""
    flat = squash(text)
    for alias, key in MAP_ALIASES.items():
        if alias in flat and key in maps:
            return key, "alias"
    hits = [k for k in maps if len(k) >= 3 and k in flat and (len(k) >= 5 or re.search(r"(?<![a-z])" + k + r"(?![a-z])", text.lower()))]
    hits += [k for k in maps if k.startswith("the") and len(k) > 6 and k[3:] in flat]
    if hits:
        return max(hits, key=len), "name"
    words = re.findall(r"[a-z0-9]+", text.lower())
    for n in (3, 2, 1):
        for i in range(len(words) - n + 1):
            part = "".join(words[i:i + n])
            if len(part) < 6:
                continue
            starts = [k for k in maps if k.startswith(part)]
            if len(starts) == 1:
                return starts[0], "prefix"
    return None, ""


def find_date(text: str) -> str | None:
    """The card's dd.mm.yyyy, as yyyy-mm-dd."""
    m = re.search(r"\b(\d{1,2})[./](\d{1,2})[./](20\d\d)\b", text)
    return f"{m.group(3)}-{int(m.group(2)):02d}-{int(m.group(1)):02d}" if m else None


def all_maps(text: str, maps: dict[str, list[str]]) -> list[str]:
    """Every map a title names, in order ("Taiga/Shambles": both)."""
    found, rest = [], text
    while (k := find_map(rest, maps)[0]) and k not in found:
        found.append(k)
        names = [a for a, key in MAP_ALIASES.items() if key == k] + [k] + ([k[3:]] if k.startswith("the") else [])
        flat = squash(rest)
        for name in names:  # cut that name out and look again
            if name in flat:
                i = flat.index(name)
                rest = flat[:i] + " " + flat[i + len(name):]
                break
        else:
            break
    return found


def identify(video: dict, maps: dict[str, list[str]]) -> dict:
    """Map and mode from the title first (the channel names both), then the opening frames."""
    title, ocr = video.get("title", ""), video.get("ocr", "")
    out = {"map": None, "map_from": "", "mode": find_mode(title)}
    out["mode_from"] = "title" if out["mode"] else ""
    key, how = find_map(title, maps)
    if key:
        out.update(map=key, map_from="title" if how == "name" else "title " + how)
    if ocr:
        # the HUD's "<Mode> in <Map>" and the welcome text's "Map: <Map>" beat the card's wording
        hud = re.findall(r"(?:map|in)\s*[:]?\s*([A-Za-z][A-Za-z0-9 _'-]{2,30})", ocr)
        if not key:
            for line in hud + [ocr]:
                k, how = find_map(line, maps)
                if k:
                    out.update(map=k, map_from="frames" if how == "name" else "frames " + how)
                    break
        if not out["mode"] and (mode := find_mode(ocr)):
            out.update(mode=mode, mode_from="frames")
        if not out["map"] and hud:
            out["map_text"] = hud[0].strip()  # a map we don't have: what the game called it
    if key and len(several := all_maps(title, maps)) > 1:
        out["other_maps"] = [k for k in several if k != key]  # a video of several matches
    out["files"] = maps.get(out["map"], [])
    out["recorded"] = find_date(ocr) or find_date(video.get("description", ""))
    return out


# --- tools -------------------------------------------------------------------------------------

def work_dir(args) -> Path:
    path = Path(args.work).resolve() if args.work else ROOT / "Tools/mfport/work/videos"
    path.mkdir(parents=True, exist_ok=True)
    return path


def ensure_packages(work: Path, *modules: str) -> None:
    """Re-run under a venv in the work folder that has these modules (pip installs them there)."""
    import importlib.util

    missing = [m for m in modules if importlib.util.find_spec(m) is None]
    if not missing:
        return
    venv = work / ".venv"
    python = venv / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
    # by prefix, not by the interpreter's path: a venv's python links to the system one
    if Path(sys.prefix).resolve() == venv.resolve():
        if os.environ.get("MFVIDEOS_INSTALLED"):
            sys.exit(f"{', '.join(missing)} won't install into {venv}")
        subprocess.check_call([sys.executable, "-m", "pip", "install", "-q", *(PIP_NAMES[m] for m in missing)])
        os.environ["MFVIDEOS_INSTALLED"] = "1"
    elif not python.exists():
        print("setting up", venv)
        subprocess.check_call([sys.executable, "-m", "venv", str(venv)])
    os.execv(str(python), [str(python), *sys.argv])


def ydl(args, **extra):
    import yt_dlp

    opts = {"quiet": True, "no_warnings": True, "sleep_interval_requests": 0.5, "retries": 5, **extra}
    if args.cookies_from_browser:
        opts["cookiesfrombrowser"] = (args.cookies_from_browser,)
    return yt_dlp.YoutubeDL(opts)


def fmt(height: int) -> str:
    """Video only (no sound needed to study a map), mp4 when there is one."""
    return f"bv*[height<={height}][ext=mp4]/bv*[height<={height}]"


def as_mp4(path: Path) -> None:
    """yt-dlp picked another container: name it as the .mp4 the next run looks for."""
    if not path.exists() and (found := next(path.parent.glob(path.stem + ".*"), None)):
        found.rename(path)


def load(work: Path) -> dict:
    path = work / "manifest.json"
    return json.loads(path.read_text()) if path.exists() else {"channel": CHANNEL, "videos": {}}


def save(work: Path, manifest: dict) -> None:
    tmp = work / "manifest.json.tmp"
    tmp.write_text(json.dumps(manifest, indent=1, ensure_ascii=False))
    tmp.replace(work / "manifest.json")
    fields = ("id", "title", "map", "mode", "other_maps", "files", "map_from", "mode_from", "uploaded", "recorded", "duration", "url")
    with open(work / "manifest.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fields, extrasaction="ignore")
        w.writeheader()
        for v in manifest["videos"].values():
            w.writerow({**v, "files": " ".join(v.get("files", [])), "other_maps": " ".join(v.get("other_maps", []))})


def apply_overrides(work: Path, manifest: dict, maps: dict[str, list[str]]) -> None:
    path = work / "overrides.json"
    if not path.exists():
        return
    for vid, fix in json.loads(path.read_text()).items():
        if vid in manifest["videos"]:
            v = manifest["videos"][vid]
            if "map" in fix:
                v.update(map=map_key(fix["map"]), map_from="override")
                v["files"] = maps.get(v["map"], [])
            if "mode" in fix:
                v.update(mode=fix["mode"], mode_from="override")


# --- commands ----------------------------------------------------------------------------------

def scan(args) -> None:
    work = work_dir(args)
    ensure_packages(work, "yt_dlp", "rapidocr_onnxruntime")
    manifest, maps = load(work), known_maps()
    videos = manifest["videos"]

    with ydl(args, extract_flat="in_playlist") as y:
        listing = y.extract_info(args.channel, download=False)
    entries = [e for e in listing.get("entries", []) if e.get("id")]
    if args.limit:
        entries = entries[: args.limit]
    print(f"{len(entries)} videos on the channel, {sum(1 for e in entries if e['id'] in videos)} known")

    intro_dir = work / "intro"
    intro_dir.mkdir(exist_ok=True)
    ocr = intro = None
    meta = ydl(args)  # one for every video: cookies and the player are read once
    for n, entry in enumerate(entries, 1):
        vid = entry["id"]
        v = videos.setdefault(vid, {"id": vid, "url": f"https://www.youtube.com/watch?v={vid}"})
        v["title"] = entry.get("title") or v.get("title", "")
        v["duration"] = entry.get("duration") or v.get("duration")
        try:
            if "uploaded" not in v:
                info = meta.extract_info(v["url"], download=False)
                v.update(uploaded=info.get("upload_date"), description=info.get("description") or "",
                         chapters=[(c["start_time"], c["title"]) for c in info.get("chapters") or []],
                         height=info.get("height"), fps=info.get("fps"))
            clip = intro_dir / f"{vid}.mp4"
            # OCR is about a minute of CPU a video: only where the title doesn't say it all
            title_says = identify({"title": v["title"]}, maps)
            if not args.no_ocr and "ocr" not in v and (args.ocr_all or not (title_says["map"] and title_says["mode"])):
                if not clip.exists():
                    if intro is None:
                        import yt_dlp

                        intro = ydl(args, format=fmt(720) + "/b[height<=720]", outtmpl=str(intro_dir / "%(id)s.%(ext)s"),
                                    download_ranges=yt_dlp.utils.download_range_func(None, [(0, INTRO_SECONDS)]))
                    intro.download([v["url"]])
                    as_mp4(clip)
                if ocr is None:
                    from rapidocr_onnxruntime import RapidOCR

                    ocr = RapidOCR()
                v["ocr"] = read_frames(clip, ocr, work)
            v.pop("error", None)
        except Exception as e:  # one bad video (private, region, throttled) must not stop the rest
            v["error"] = str(e)[:300]
            print(f"  {vid}: {v['error']}")
        v.update(identify(v, maps))
        print(f"[{n}/{len(entries)}] {vid} {v.get('map') or '?':>14} {v.get('mode') or '?':>8}  {v['title'][:60]}")
        if n % 10 == 0:
            save(work, manifest)
    apply_overrides(work, manifest, maps)
    save(work, manifest)
    print("manifest:", work / "manifest.csv")


def read_frames(clip: Path, ocr, work: Path) -> str:
    """Every line OCR finds in the opening frames, once each, in order."""
    lines: list[str] = []
    still = work / "ocr_frame.png"
    for t in OCR_TIMES:
        subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(t), "-i", str(clip), "-frames:v", "1", str(still)], check=False)
        if not still.exists():
            break
        result, _ = ocr(str(still))
        for _, text, score in result or []:
            if score >= 0.6 and text not in lines:
                lines.append(text)
        still.unlink()
    return "\n".join(lines)


def report(args) -> None:
    work = work_dir(args)
    manifest, maps = load(work), known_maps()
    for v in manifest["videos"].values():  # names may have been fixed since the scan
        v.update(identify(v, maps))
    apply_overrides(work, manifest, maps)
    save(work, manifest)
    by_map: dict[str, list[dict]] = {}
    for v in manifest["videos"].values():
        by_map.setdefault(v.get("map") or "?" + (v.get("map_text") or ""), []).append(v)
    have = sorted((k for k in by_map if k in maps), key=lambda k: -len(by_map[k]))
    print(f"{len(manifest['videos'])} videos, {sum(len(by_map[k]) for k in have)} on {len(have)} maps we have\n")
    for k in have:
        modes = sorted({v.get("mode") or "?" for v in by_map[k]})
        hours = sum(v.get("duration") or 0 for v in by_map[k]) / 3600
        print(f"{k:16} {len(by_map[k]):3} videos {hours:5.1f} h  {' '.join(modes):30} {' '.join(maps[k])}")
    other = [k for k in by_map if k not in maps]
    if other:
        print("\nnot matched to a map we have (fix in overrides.json):")
        for k in sorted(other):
            for v in by_map[k]:
                print(f"  {v['id']}  {k:20} {v.get('title', '')[:70]}")


def newest_first(videos: list[dict]) -> list[dict]:
    return sorted(videos, key=lambda v: v.get("recorded") or v.get("uploaded") or "", reverse=True)


def picked_videos(manifest: dict, args) -> list[dict]:
    want = map_key(args.map) if args.map else None
    return [v for v in manifest["videos"].values()
            if (not want or v.get("map") == want) and (not args.mode or v.get("mode") == args.mode)
            and (not args.id or v["id"] in args.id)]


def full_path(work: Path, v: dict) -> Path:
    date = (v.get("recorded") or v.get("uploaded") or "unknown").replace("-", "")
    return work / "full" / (v.get("map") or "unknown") / f"{date}_{v['id']}.mp4"


def download(args) -> None:
    """Whole videos, to study a map everywhere: the newest per map unless --all (newest = how the
    game looks now; older ones may show things changed since)."""
    work = work_dir(args)
    ensure_packages(work, "yt_dlp")
    manifest = load(work)
    chosen: list[dict] = []
    by_map: dict[str, list[dict]] = {}
    for v in picked_videos(manifest, args):
        if v.get("map") and not v.get("other_maps"):  # one map per file, so its stills are that map
            by_map.setdefault(v["map"], []).append(v)
    for videos in by_map.values():
        chosen += newest_first(videos)[: None if args.all else args.per_map]
    hours = sum(v.get("duration") or 0 for v in chosen) / 3600
    print(f"{len(chosen)} videos, {hours:.1f} h, about {hours * 1.3:.0f} GB at 720p")
    for n, v in enumerate(newest_first(chosen), 1):
        out = full_path(work, v)
        if out.exists():
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        print(f"[{n}/{len(chosen)}] {v.get('map')} {v['id']} {v.get('title', '')[:60]}")
        try:  # no sound: lighting and play are what's studied; keeps it small
            with ydl(args, format=fmt(args.height), outtmpl=str(out.with_suffix(".%(ext)s"))) as y:
                y.download([v["url"]])
            as_mp4(out)
        except Exception as e:
            print(f"  {v['id']}: {str(e)[:200]}")


def frames(args) -> None:
    """Stills every N seconds, from the download if there is one, else straight from the stream
    (seeking fetches only those bytes)."""
    work = work_dir(args)
    ensure_packages(work, "yt_dlp")
    manifest = load(work)
    picked = picked_videos(manifest, args)
    print(f"{len(picked)} videos")
    for v in picked:
        out = work / "frames" / (v.get("map") or "unknown")
        out.mkdir(parents=True, exist_ok=True)
        times = range(args.start, int(v.get("duration") or 0), args.every)
        todo = [t for t in times if not (out / f"{v['id']}_{t:05d}.jpg").exists()]
        if not todo:
            continue
        local = full_path(work, v)
        stream = str(local)
        if not local.exists():
            try:
                with ydl(args, format=fmt(args.height)) as y:
                    info = y.extract_info(v["url"], download=False)
                stream = info.get("url") or info["requested_formats"][0]["url"]
            except Exception as e:
                print(f"  {v['id']}: {str(e)[:200]}")
                continue
        for t in todo:
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-ss", str(t), "-i", stream, "-frames:v", "1",
                            "-q:v", "3", str(out / f"{v['id']}_{t:05d}.jpg")], check=False)
        print(f"{v['id']} {v.get('map')} {len(todo)} stills -> {out}")
        time.sleep(1)


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--work", help="output folder (default Tools/mfport/work/videos)")
    p.add_argument("--cookies-from-browser", help="firefox, chrome ...: if YouTube asks to sign in")
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("scan", help="list the channel, read titles and opening frames")
    s.add_argument("--channel", default=CHANNEL)
    s.add_argument("--limit", type=int, help="only the newest N videos")
    s.add_argument("--no-ocr", action="store_true", help="titles only, no downloads")
    s.add_argument("--ocr-all", action="store_true", help="read the opening frames of every video, not only "
                   "where the title misses the map or mode (dates off the title card; hours)")
    s.set_defaults(func=scan)
    sub.add_parser("report", help="videos per map").set_defaults(func=report)
    def pick(cmd):
        cmd.add_argument("--map", help="map file or name, e.g. mf-warehouse")
        cmd.add_argument("--mode", help="tdm, ctf ...")
        cmd.add_argument("--id", nargs="*", help="only these video ids")
        cmd.add_argument("--height", type=int, default=720)

    d = sub.add_parser("download", help="whole videos, the newest of each map")
    pick(d)
    d.add_argument("--per-map", type=int, default=1, help="newest N of each map (default 1)")
    d.add_argument("--all", action="store_true", help="every video of the picked maps")
    d.set_defaults(func=download)
    f = sub.add_parser("frames", help="stills for lighting comparison")
    pick(f)
    f.add_argument("--every", type=int, default=30, help="seconds between stills (default 30)")
    f.add_argument("--start", type=int, default=15, help="first still, past the title card")
    f.set_defaults(func=frames)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
