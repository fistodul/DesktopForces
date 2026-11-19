"""WeapTools replacement actors change only authored pickups in maps that use them."""
import copy
from pathlib import Path
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mfport'))
from mflib.actors import ActorMapper, PICKUP_CLASSES, apply_weapon_replacers

class WeapToolsTests(unittest.TestCase):
    def test_normal_map_keeps_manual_pickup_and_authored_ammo(self):
        rows = [dict(type='pickup', weapon=PICKUP_CLASSES['Pistol'], max_clips=40, respawn_time=30)]
        before = copy.deepcopy(rows)
        self.assertEqual(apply_weapon_replacers(rows), before)

    def test_replacer_uses_class_default_ammo_once_and_leaves_other_items(self):
        rows = [dict(type='legacy', mf_class='WeapReplacer'),
                dict(type='pickup', weapon=PICKUP_CLASSES['Pistol'], max_clips=40, max_clip_ammo=90, respawn_time=30),
                dict(type='pickup', weapon='/Script/DesktopForces.Knife', max_clips=1, respawn_time=30)]
        untouched = rows[2].copy()
        apply_weapon_replacers(rows)
        self.assertTrue(rows[1]['auto_pickup'])
        self.assertEqual((rows[1]['respawn_time'], rows[1]['max_clips'], rows[1]['max_clip_ammo']), (0, 0, 0))
        self.assertEqual(rows[1]['radius'], 49.5)
        self.assertEqual(rows[2], untouched)

    def test_direct_pickup_resolves_weapon_and_touch_size(self):
        actor = dict(name='Pickup0', **{'class':'WeapPickup'}, location=[0,0,0],
                     rotation=dict(pitch=0,yaw=0,roll=0),
                     props={'WeaponClass':"Class'RageWeapons.AdrenalineShot'", 'CollisionRadius':'30'})
        rows = ActorMapper({}, {}).map(actor)
        pickup = next(r for r in rows if r['type']=='pickup')
        self.assertEqual(pickup['weapon'], '/Script/DesktopForces.AdrenalineShot')
        self.assertTrue(pickup['auto_pickup'])
        self.assertEqual((pickup['radius'],pickup['height'],pickup['respawn_time']), (67.5,49.5,0))
