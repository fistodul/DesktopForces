"""Read UE2-style MeshAnimation objects (skeletal animation) from Mobile Forces packages.

Layout (verified on RageWeapons.u, consumes every byte of each export):
  RefBones:  array of (FName Name, DWORD Flags, INT ParentIndex)
  Moves:     array of MotionChunk (RootSpeed3D, TrackTime, StartBone, Flags,
             BoneIndices[], AnimTracks[] (Flags, KeyQuat[], KeyPos[], KeyTime[]), RootTrack)
  AnimSeqs:  array of (FName Name, FName Groups[], INT StartFrame, INT NumFrames,
             Notifys[] (float Time, FName Function), float Rate)

UE1/MF animation timing: PlayAnim(Seq, Rate, TweenTime) lasts
  (NumFrames - 1) / (Seq.Rate * Rate) + TweenTime  seconds,
which is what weapon fire/reload/select times in Source/DesktopForces/Weapons are based on.

Usage:  python3 -m mflib.anims <package.u> [AnimationName]
"""
from __future__ import annotations

import sys
from dataclasses import dataclass

from .upkg import Package, Reader


@dataclass
class AnimTrack:
    flags: int
    quats: list
    positions: list
    times: list[float]


@dataclass
class AnimSeq:
    name: str
    groups: list[str]
    start_frame: int
    num_frames: int
    rate: float
    notifies: list[tuple[float, str]]

    def duration(self, play_rate: float = 1.0, tween: float = 0.0) -> float:
        return (self.num_frames - 1) / (self.rate * play_rate) + tween if self.rate else tween


@dataclass
class MeshAnimation:
    bones: list[tuple[str, int, int]]
    moves: list[dict]
    seqs: list[AnimSeq]


def read_animation(pkg: Package, name: str) -> MeshAnimation:
    exp = pkg.find(name, 'Animation')
    obj = pkg.read_object(exp)
    r = Reader(pkg.data, obj.native_offset)
    bones = [(pkg.names[r.index()], r.u32(), r.i32()) for _ in range(r.index())]

    def track() -> AnimTrack:
        flags = r.u32()
        quats = [(r.f32(), r.f32(), r.f32(), r.f32()) for _ in range(r.index())]
        pos = [r.vec() for _ in range(r.index())]
        times = [r.f32() for _ in range(r.index())]
        return AnimTrack(flags, quats, pos, times)

    moves = []
    for _ in range(r.index()):
        root_speed, track_time, start_bone, flags = r.vec(), r.f32(), r.i32(), r.u32()
        bone_indices = [r.i32() for _ in range(r.index())]
        tracks = [track() for _ in range(r.index())]
        root = track()
        moves.append({'root_speed': root_speed, 'track_time': track_time, 'start_bone': start_bone,
                      'flags': flags, 'bone_indices': bone_indices, 'tracks': tracks, 'root': root})
    seqs = []
    for _ in range(r.index()):
        sname = pkg.names[r.index()]
        groups = [pkg.names[r.index()] for _ in range(r.index())]
        start, frames = r.i32(), r.i32()
        notifies = [(r.f32(), pkg.names[r.index()]) for _ in range(r.index())]
        rate = r.f32()
        seqs.append(AnimSeq(sname, groups, start, frames, rate, notifies))
    if r.p != exp.offset + exp.size:
        raise ValueError(f'{name}: parsed {r.p - exp.offset} of {exp.size} bytes')
    return MeshAnimation(bones, moves, seqs)


def main():
    pkg = Package(sys.argv[1])
    names = sys.argv[2:] or [e.name for e in pkg.exports_of('Animation')]
    for n in names:
        anim = read_animation(pkg, n)
        print(f'== {n}: {len(anim.bones)} bones, {len(anim.seqs)} sequences')
        for s in anim.seqs:
            print(f'   {s.name:28} frames={s.num_frames:3} rate={s.rate:6.2f}  len@1.0={s.duration():.3f}s')


if __name__ == '__main__':
    main()
