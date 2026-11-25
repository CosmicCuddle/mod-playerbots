import unittest
from bag_evidence import free_carried_slots, full_bag_problems

BACKPACK = [(0, slot, 100 + slot, 25) for slot in range(23, 39)]
GEAR = [(0, 4, 1, 20)]
BAG = [(0, 19, 200, 30)]
CONTENTS = [(200, slot, 300 + slot, 25) for slot in range(4)]
FULL = GEAR + BACKPACK + BAG + CONTENTS


class BagEvidenceTests(unittest.TestCase):
    def test_full(self):
        self.assertEqual(full_bag_problems(FULL, {30: 4}), [])
        self.assertEqual(full_bag_problems(GEAR + BACKPACK, {}), [])

    def test_empty_slot(self):
        self.assertEqual(free_carried_slots(FULL[:-1], {30: 4}), 1)
        self.assertTrue(full_bag_problems(FULL[:-1], {30: 4}))

    def test_bank_does_not_fill_backpack(self):
        rows = GEAR + BACKPACK[:-1] + [(0, 39, 900, 25)]
        self.assertEqual(free_carried_slots(rows, {}), 1)

    def test_invalid_size(self):
        for sizes in ({}, {30: 0}, {30: 37}, {30: True}):
            with self.subTest(sizes=sizes):
                self.assertTrue(full_bag_problems(FULL, sizes))

    def test_out_of_range(self):
        rows = FULL[:-1] + [(200, 4, 303, 25)]
        self.assertTrue(full_bag_problems(rows, {30: 4}))

    def test_duplicate_position_or_guid(self):
        for row in ((200, 0, 999, 25), (200, 5, 300, 25)):
            with self.subTest(row=row):
                self.assertTrue(full_bag_problems(FULL + [row], {30: 4}))

    def test_no_gear(self):
        self.assertTrue(full_bag_problems(BACKPACK, {}))
        self.assertTrue(full_bag_problems(BACKPACK + [(0, 3, 1, 20)], {}))


if __name__ == '__main__':
    unittest.main()
