"""Collision/layout contracts, independent of the external map compiler."""
import unittest
from collections import deque
from strafegen_nocturne_flow import NocturneFlow
from strafegen_geom import validate_spawns


class FlowLayout(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.course = NocturneFlow(64, night=True).build()

    def clear(self, x, y, feet=2):
        return not any(all(a < d and c < b for a,b,c,d in zip(
            (x-15,y-15,feet),(x+15,y+15,feet+56),brush.mins,brush.maxs))
            for brush in self.course.solids)

    def test_spawn_hulls(self):
        self.assertEqual(validate_spawns(self.course),10)

    def test_objective_courts_clear(self):
        locations=[e for e in self.course.entities if e['classname']=='target_location']
        self.assertEqual(len(locations),3)
        for e in locations:
            x,y,_=map(float,e['origin'].split())
            for dx in (-96,0,96):
                for dy in (-96,0,96):
                    self.assertTrue(self.clear(x+dx,y+dy))

    def test_ground_routes_connect(self):
        # Conservative walking-only grid: cover and raised galleries are solid.
        # All three courts and the eight ground spawns must connect without tricks.
        open_cells={(x,y) for x in range(-1472,1473,64)
                    for y in range(-1664,1665,64) if self.clear(x,y)}
        visited={(0,0)}; queue=deque(visited)
        while queue:
            x,y=queue.popleft()
            for q in ((x-64,y),(x+64,y),(x,y-64),(x,y+64)):
                if q in open_cells and q not in visited:
                    visited.add(q);queue.append(q)
        for point in ((0,-1024),(0,1024)):
            self.assertIn(point,visited)
        for e in self.course.entities:
            if e['classname']=='info_player_deathmatch':
                x,y,z=map(float,e['origin'].split())
                if z==40:
                    self.assertIn((round(x/64)*64,round(y/64)*64),visited)

    def test_traversal_nodes_clear_and_supported(self):
        routes=[e for e in self.course.entities if e['classname']=='info_bot_route']
        self.assertEqual(len(routes),12)
        for e in routes:
            for key in ('origin','exit'):
                x,y,z=map(float,e[key].split())
                self.assertTrue(self.clear(x,y,z-24+.2),(e['message'],key))
                self.assertFalse(self.clear(x,y,z-24-2),(e['message'],key,'unsupported'))
            if int(e['spawnflags']) & 1:
                x,y,z=map(float,e['contact'].split())
                self.assertTrue(self.clear(x,y,z-24),(e['message'],'contact'))
                self.assertFalse(self.clear(x+(26 if x>0 else -26),y,z-24))

    def test_no_external_reservoir_or_mirror(self):
        self.assertFalse(getattr(self.course,'extra_pk3',[]))
        self.assertTrue(self.course.bake_vis)
        for brush in self.course.solids:
            for face in brush.faces:
                self.assertNotIn('nrx_',face.tex)
                self.assertNotIn('mirror',face.tex)


if __name__=='__main__':
    unittest.main()
