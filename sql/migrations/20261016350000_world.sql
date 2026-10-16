DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016350000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016350000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk, Phase 3 bosses: signature moves for the other 21 stage
-- bosses (Heroic, Mythic, the raids), drafted in docs/zones/boss_moves.sql
-- and reviewed. Removed with them: King Mosh's Vicious Rend (a bleed) and
-- Hakkar's unwarned Shadow Bolt Volley cone; Ossirian's 20 yd War Stomp
-- (never cast); stand-ins the new moves replace (Theradras' and Archaedas'
-- Trample, Balnazzar's Shadow Nova II, Gahz'rilla's Slam circle, King Mosh's
-- Trample) and plain-cast Cleaves now warned as cones (Drakkisath,
-- Rivendare, Kazzak). Warned area crowd control (War Stomp, Inferno, Panic)
-- stays, as for Molten Core.
-- =============================================================

-- =============================================================
-- Vanilla Bonk: stage boss signature moves (bonk_mob_ability.shape). Reviewed 2026-10-16.
-- Every stage boss (bonk_map_creature.kind = 2) without moves yet: Heroic maps 7-13 two each,
-- Mythic maps 14-19 and the raids (2 Molten Core, 5 Zul'Gurub, 6 Ruins of Ahn'Qiraj) two or
-- three, at most one held back by below_health. Edwin VanCleef 639, Stitches 412, Charlga
-- Razorflank 4421 (and Mr. Smite 646) have theirs (migration 20261016330000).
--
-- Shapes (BonkMobAI::StartShape / UpdateArea): 1 cone, 90 degrees, `size` long, at the hero;
-- 2 ring round the boss, `size` outer radius, safe within half of it; 3 barrage, `count`
-- circles of radius `size` round the hero (the first where they will be, the rest within
-- 10 yd, 0.3 s apart); 4 trail (not used here: the code already brings adds at 66/33%).
-- Cones and rings stop the boss for the 1.5 s fill. The spell only names the move (never
-- cast); `damage` is in the boss's melee hits.
-- target: 0 for cones and barrages (need line of sight, as Stitches' and Edwin's), 1 for
-- rings (as Charlga's and Mr. Smite's). Sizes sit on stand-in sizes (half yards to 8, whole
-- yards after): cones 8-10, rings 12-14, barrage circles 3-5. Cones are used with the hero
-- within size-1 yd, rings within the outer radius.
--
-- Notes for the reviewer:
-- * Apply AFTER docs/zones/heroic_*.sql and mythic_*.sql (and anything re-running them):
--   those DELETE every ability row of these bosses, which would wipe these moves.
-- * A boss tries its abilities in spell_id order (BonkMgr::LoadMobs ORDER BY entry,
--   spell_id; the first ready one in range wins), so a move with a lower id goes first
--   when two are ready together.
-- * Rows that now overlap a new move, or break the "no stun/root/fear/DoT on the hero"
--   rule, are listed per boss as COMMENTED-OUT DELETEs with the reason; nothing here
--   removes an existing row.
-- * Large bodies: Magmadar and The Beast (combat reach 12), Ossirian (12.5), Hakkar (10.5)
--   and Lord Kazzak (body about 6.6 yd at display scale 2.2) already hit from about as far
--   as a 10 yd cone or a 14 yd ring reach, so for them the cone/ring is extra burst up
--   close and the barrage carries the fight at range.
-- =============================================================


-- =============================================================
-- Heroic
-- =============================================================

-- Gahz'rilla (7273), Tanaris. The Zul'Farrak hydra: Gahz'rilla Slam and Icicle. Has an
-- Icicle bolt (11131) and the Slam as a 10 yd circle round him (11902). Both real ids are
-- taken, so the moves borrow names: Ground Smash for the Slam as a ring, Hail Storm for an
-- icicle barrage.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 7273 AND `spell_id` IN (10737, 12734);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(7273, 10737, 0, 9000,  6000, 0, 40, 1.0, 3, 5, 4,  100, 'Gahz''rilla: Hail Storm, 5 icicles (4 yd circles) crash down round the hero'),
(7273, 12734, 1, 10000, 5000, 0, 13, 1.5, 2, 0, 13, 100, 'Gahz''rilla: Ground Smash, his Gahz''rilla Slam as a ring 13 yd round him (safe within 6.5)');
-- Overlap: the old Slam circle hits up close, the opposite of the new ring. If it goes,
-- Ground Smash can be re-keyed to 11902 to carry the real name.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 7273 AND `spell_id` = 11902;   -- Gahz'rilla Slam circle, replaced by the Ground Smash ring

-- King Mosh (6584), Un'Goro Crater. The devilsaur king: a stomping ring and a bite.
-- Has Vicious Rend (14331, a bleed on the hero), Trample (15550, 10 yd circle), Charge.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 6584 AND `spell_id` IN (12612, 17257);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(6584, 12612, 1, 10000, 5000, 0, 12, 1.5, 2, 0, 12, 100, 'King Mosh: Stomp, a devilsaur footfall, a ring 12 yd round him (safe within 6)'),
(6584, 17257, 0, 7000,  3000, 0, 8,  1.6, 1, 0, 9,  100, 'King Mosh: Bite, a cone 9 yd in front of him');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 6584 AND `spell_id` = 14331;   -- Vicious Rend: a bleed (DoT) on the hero, against the no-DoT rule
DELETE FROM `bonk_mob_ability` WHERE `entry` = 6584 AND `spell_id` = 15550;   -- optional: Trample circle (hits up close) works against the Stomp ring's safe middle

-- Mosh'Ogg Lord (680), Stranglethorn Vale. An ogre lord: a head-cracking club swing, and
-- rocks thrown at a hero who keeps away. Has Thunderclap (8147, 10 yd circle) and Charge.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 680 AND `spell_id` IN (4164, 16172);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(680, 4164,  0, 10000, 6000, 0, 35, 1.1, 3, 4, 3.5, 100, 'Mosh''Ogg Lord: Throw Rock, 4 rocks (3.5 yd circles) land round the hero'),
(680, 16172, 0, 7000,  3000, 0, 8,  1.6, 1, 0, 9,   100, 'Mosh''Ogg Lord: Head Crack, an overhead club swing, a cone 9 yd in front of him');

-- Princess Theradras (12201), Desolace. Maraudon: Dust Field round her, Repulsive Gaze in
-- front, Boulder. Has Thrash, Boulder (21832) and Trample (5568), the stand-in for her Dust Field.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 12201 AND `spell_id` IN (21869, 21909);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(12201, 21869, 0, 8000,  3000, 0, 9,  1.5, 1, 0, 10, 100, 'Princess Theradras: Repulsive Gaze, a cone 10 yd in front of her'),
(12201, 21909, 1, 11000, 5000, 0, 14, 1.4, 2, 0, 14, 100, 'Princess Theradras: Dust Field, a ring 14 yd round her (safe within 7)');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 12201 AND `spell_id` = 5568;   -- Trample, the Dust Field stand-in: the Dust Field ring is the real one now

-- Archaedas (2748), Badlands. The Uldaman stone keeper: Ground Tremor as a ring, boulders at
-- range. Has Boulder (9483, a bolt), Knock Away, and Trample (5568), the stand-in for his
-- Ground Tremor.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 2748 AND `spell_id` IN (6524, 21071);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(2748, 6524,  1, 10000, 4000, 0, 13, 1.5, 2, 0, 13,  100, 'Archaedas: Ground Tremor, a ring 13 yd round him (safe within 6.5)'),
(2748, 21071, 0, 11000, 6000, 0, 40, 1.1, 3, 4, 4.5, 100, 'Archaedas: Boulder, 4 boulders (4.5 yd circles) fall round the hero');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 2748 AND `spell_id` = 5568;   -- Trample, the Ground Tremor stand-in: the Ground Tremor ring is the real one now
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 2748 AND `spell_id` = 9483;   -- optional: the Boulder bolt beside the Boulder barrage (two boulder moves)

-- Fozruk (2611), Arathi Highlands. A mountain giant: a smashing fist in front, the ground
-- splitting under a hero at range. Has Trample (5568, his Ground Tremor stand-in, kept: no
-- ring here), Boulder (9483, a bolt), Knock Away.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 2611 AND `spell_id` IN (18944, 19798);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(2611, 18944, 0, 8000,  3000, 0, 9,  1.6, 1, 0, 10,  100, 'Fozruk: Smash, a giant''s fist, a cone 10 yd in front of him'),
(2611, 19798, 0, 11000, 6000, 0, 40, 1.1, 3, 4, 4.5, 100, 'Fozruk: Earthquake, the ground splits round the hero, 4 circles (4.5 yd)');

-- Avatar of Hakkar (8443), Swamp of Sorrows. The Sunken Temple avatar: Lash in front, and
-- Hakkar's acid at range. Has Flame of Hakkar (12354, 10 yd circle) and Venom Spit (a bolt).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 8443 AND `spell_id` IN (6607, 12280);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(8443, 6607,  0, 8000,  3000, 0, 9,  1.5, 1, 0, 10, 100, 'Avatar of Hakkar: Lash, a cone 10 yd in front of him'),
(8443, 12280, 0, 10000, 6000, 0, 35, 1.1, 3, 4, 4,  100, 'Avatar of Hakkar: Acid of Hakkar, 4 splashes of acid (4 yd circles) round the hero');


-- =============================================================
-- Mythic
-- =============================================================

-- Emperor Dagran Thaurissan (9019), Burning Steppes. Avatar of Flame round him, Ironfoe
-- swings, and from 50% fire falls. Has Fireball (a bolt), Ironfoe (extra swings) and
-- Blast Wave (15744, 10 yd circle).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 9019 AND `spell_id` IN (15636, 19132, 19717);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(9019, 15636, 1, 11000, 5000, 0, 12, 1.5, 2, 0, 12, 100, 'Emperor Dagran Thaurissan: Avatar of Flame, a ring 12 yd round him (safe within 6)'),
(9019, 19132, 0, 7000,  3000, 0, 7,  1.6, 1, 0, 8,  100, 'Emperor Dagran Thaurissan: Mighty Blow, an Ironfoe swing, a cone 8 yd in front of him'),
(9019, 19717, 0, 10000, 4000, 0, 40, 1.2, 3, 5, 4,  50,  'Emperor Dagran Thaurissan: from 50%, Rain of Fire, 5 circles (4 yd) round the hero');
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 9019 AND `spell_id` = 15744;   -- optional: Blast Wave circle (fire round him, hits up close) next to the Avatar of Flame ring

-- General Drakkisath (10363), Burning Steppes. UBRS: Cleave, Flamestrike at range, and
-- Conflagration from 50%. Has Thunderclap (15588, 10 yd circle), Cleave (15754, a plain
-- cast) and Rage.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10363 AND `spell_id` IN (16419, 16805, 20691);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(10363, 16419, 0, 10000, 5000, 0, 40, 1.2, 3, 4, 5,  100, 'General Drakkisath: Flamestrike, 4 circles (5 yd) round the hero'),
(10363, 16805, 1, 12000, 3000, 0, 14, 1.8, 2, 0, 14, 50,  'General Drakkisath: from 50%, Conflagration, a ring of fire 14 yd round him (safe within 7)'),
(10363, 20691, 0, 7000,  3000, 0, 9,  1.6, 1, 0, 10, 100, 'General Drakkisath: Cleave, a cone 10 yd in front of him');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10363 AND `spell_id` = 15754;   -- Cleave as a plain cast: the Cleave cone is the warned version
-- (His Thunderclap circle, 10 yd, still hits up close: from 50% that and the ring together leave little safe ground; reviewer's call.)

-- Baron Rivendare (10440), Eastern Plaguelands. Stratholme: Unholy Aura round him and the
-- runeblade's cleave. Has Shadow Bolt (a bolt), Mortal Strike, Cleave (15754, a plain cast).
-- (His Raise Dead would be a trail; left out, the code already brings adds.)
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10440 AND `spell_id` IN (15284, 17467);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(10440, 15284, 0, 7000,  3000, 0, 8,  1.6, 1, 0, 9,  100, 'Baron Rivendare: Cleave, a runeblade sweep, a cone 9 yd in front of him'),
(10440, 17467, 1, 10000, 5000, 0, 12, 1.4, 2, 0, 12, 100, 'Baron Rivendare: Unholy Aura, a ring 12 yd round him (safe within 6)');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10440 AND `spell_id` = 15754;   -- Cleave as a plain cast: the Cleave cone is the warned version

-- Balnazzar (10813), Eastern Plaguelands. The dreadlord behind Grand Crusader Dathrohan:
-- Shadow Shock round him, the Crusader's strike kept from his disguise, and from 50% a
-- shadow storm. Has Mind Blast (a bolt) and Shadow Nova II (1112, 10 yd circle), the
-- stand-in for his Shadow Shock.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10813 AND `spell_id` IN (14297, 17281, 17289);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(10813, 14297, 0, 10000, 4000, 0, 40, 1.2, 3, 5, 4,  50,  'Balnazzar: from 50%, Shadow Storm, 5 circles (4 yd) round the hero'),
(10813, 17281, 0, 7000,  3000, 0, 9,  1.6, 1, 0, 10, 100, 'Balnazzar: Crusader Strike (Dathrohan''s), a cone 10 yd in front of him'),
(10813, 17289, 1, 10000, 5000, 0, 14, 1.5, 2, 0, 14, 100, 'Balnazzar: Shadow Shock, a ring 14 yd round him (safe within 7)');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10813 AND `spell_id` = 1112;   -- Shadow Nova II, the Shadow Shock stand-in: the Shadow Shock ring is the real one now

-- Darkmaster Gandling (1853), Western Plaguelands. Scholomance: Arcane Missiles, and his
-- Shadow Portals opening round him. Has Shadow Bolt and Shadow Shock (bolts) and Bone Armor.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 1853 AND `spell_id` IN (15791, 17950);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(1853, 15791, 0, 9000,  4000, 0, 35, 1.0, 3, 5, 3.5, 100, 'Darkmaster Gandling: Arcane Missiles, 5 missiles (3.5 yd circles) strike round the hero'),
(1853, 17950, 1, 12000, 6000, 0, 12, 1.5, 2, 0, 12,  100, 'Darkmaster Gandling: Shadow Portal, a ring of portals 12 yd round him (safe within 6)');

-- Prince Tortheldrin (11486), Feralas. Dire Maul West: Arcane Blast up close, arcane bombs
-- at range, and from 50% an Arcane Explosion. Has Thrash, Arcane Bolt (a bolt) and
-- Whirlwind (15589, 8 yd circle).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 11486 AND `spell_id` IN (19821, 22271, 22920);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(11486, 19821, 0, 10000, 6000, 0, 35, 1.1, 3, 4, 4,  100, 'Prince Tortheldrin: Arcane Bomb, 4 circles (4 yd) round the hero'),
(11486, 22271, 1, 12000, 3000, 0, 13, 1.7, 2, 0, 13, 50,  'Prince Tortheldrin: from 50%, Arcane Explosion, a ring 13 yd round him (safe within 6.5)'),
(11486, 22920, 0, 8000,  3000, 0, 8,  1.6, 1, 0, 9,  100, 'Prince Tortheldrin: Arcane Blast, a cone 9 yd in front of him');

-- King Gordok (11501), Feralas. Dire Maul North: the ogre king's War Stomp and club.
-- Has Mortal Strike, Berserker Charge and War Stomp (16727, 5 yd circle that stuns).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 11501 AND `spell_id` IN (11876, 14099);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(11501, 11876, 1, 10000, 5000, 0, 13, 1.5, 2, 0, 13, 100, 'King Gordok: War Stomp, a ring 13 yd round him (safe within 6.5)'),
(11501, 14099, 0, 7000,  3000, 0, 9,  1.7, 1, 0, 10, 100, 'King Gordok: Mighty Blow, a cone 10 yd in front of him');
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 11501 AND `spell_id` = 16727;   -- War Stomp circle: stuns the hero (no-stun rule), and the War Stomp ring replaces it

-- Warchief Rend Blackhand (10429), Searing Gorge. UBRS, on Gyth: the drake's Flame Breath
-- and Corrosive Acid. Has Enrage, Whirlwind (15589, 8 yd circle), Mortal Strike, Cleave.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10429 AND `spell_id` IN (8245, 16390);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(10429, 8245,  0, 10000, 6000, 0, 35, 1.1, 3, 4, 4,  100, 'Warchief Rend Blackhand: Corrosive Acid, Gyth spits 4 circles (4 yd) round the hero'),
(10429, 16390, 0, 8000,  4000, 0, 9,  1.6, 1, 0, 10, 100, 'Warchief Rend Blackhand: Flame Breath, Gyth''s breath, a cone 10 yd in front');

-- The Beast (10430), Searing Gorge. UBRS: Fire Blast up close, fireballs at range.
-- Has Berserker Charge, Flamebreak (16785, 10 yd circle) and Fireball (16788, a bolt).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 10430 AND `spell_id` IN (11988, 20623);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(10430, 11988, 0, 10000, 5000, 0, 40, 1.1, 3, 5, 4,  100, 'The Beast: Fireball Volley, 5 fireballs (4 yd circles) land round the hero'),
(10430, 20623, 0, 8000,  3000, 0, 9,  1.6, 1, 0, 10, 100, 'The Beast: Fire Blast, a cone 10 yd in front of him');
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 10430 AND `spell_id` = 16788;   -- optional: the Fireball bolt beside the Fireball Volley barrage

-- Lord Kazzak (12397), Felwood. The Blasted Lands doomlord: Cleave, Shadow Bolt Volley, and
-- from 50% his Marks bursting. Has Thunderclap (8078, 10 yd circle), Cleave (16044, a plain
-- cast) and Void Bolt (a bolt).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 12397 AND `spell_id` IN (15496, 21056, 21341);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(12397, 15496, 0, 7000,  3000, 0, 9,  1.7, 1, 0, 10,  100, 'Lord Kazzak: Cleave, a cone 10 yd in front of him'),
(12397, 21056, 0, 12000, 3000, 0, 40, 1.6, 3, 3, 5,   50,  'Lord Kazzak: from 50%, Mark of Kazzak, 3 marks burst round the hero (5 yd circles)'),
(12397, 21341, 0, 9000,  5000, 0, 40, 1.0, 3, 5, 3.5, 100, 'Lord Kazzak: Shadow Bolt Volley, 5 bolts (3.5 yd circles) land round the hero');
DELETE FROM `bonk_mob_ability` WHERE `entry` = 12397 AND `spell_id` = 16044;   -- Cleave as a plain cast: the Cleave cone is the warned version

-- Kroshius (14467), Felwood. The Shatter Scar infernal: a fel stomp round him, infernals
-- crashing down at range. Has Knock Away, Immolation (20153, 8 yd circle) and Inferno
-- Effect (22703, 10 yd circle that stuns).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 14467 AND `spell_id` IN (7139, 19695);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(14467, 7139,  1, 10000, 4000, 0, 13, 1.5, 2, 0, 13, 100, 'Kroshius: Fel Stomp, a ring 13 yd round him (safe within 6.5)'),
(14467, 19695, 0, 11000, 6000, 0, 40, 1.3, 3, 3, 5,  100, 'Kroshius: Inferno, 3 infernals crash down round the hero (5 yd circles)');
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 14467 AND `spell_id` = 22703;   -- Inferno Effect: stuns the hero (no-stun rule); the Inferno barrage is the landing now
-- (His Immolation circle, 8 yd, still pulses up close, inside the Fel Stomp ring's safe middle; reviewer's call.)


-- =============================================================
-- Raids
-- =============================================================

-- Magmadar (11982), Molten Core. The core hound: Lava Breath in front, Lava Bombs at range,
-- and from 50% magma spat everywhere. Has Frenzy and Panic (19408, 10 yd circle that fears).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 11982 AND `spell_id` IN (19272, 19411, 19449);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(11982, 19272, 0, 8000,  4000, 0, 9,  1.7, 1, 0, 10,  100, 'Magmadar: Lava Breath, a cone 10 yd in front of him'),
(11982, 19411, 0, 10000, 6000, 0, 40, 1.2, 3, 4, 4.5, 100, 'Magmadar: Lava Bomb, 4 circles (4.5 yd) burst round the hero'),
(11982, 19449, 0, 9000,  3000, 0, 40, 1.0, 3, 6, 3,   50,  'Magmadar: from 50%, Magma Spit, 6 small circles (3 yd) spatter round the hero');
-- DELETE FROM `bonk_mob_ability` WHERE `entry` = 11982 AND `spell_id` = 19408;   -- Panic: fears the hero (no-fear rule)

-- Hakkar (14834), Zul'Gurub. The Soulflayer: Corrupted Blood at range, the Aspect of Venoxis
-- poison nova round him, and from 50% Blood Siphon in front. Has Shadow Shock (20603, 10 yd
-- circle) and Shadow Bolt Volley (20741).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 14834 AND `spell_id` IN (24322, 24328, 24688);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(14834, 24322, 0, 8000,  3000, 0, 9,  1.8, 1, 0, 10, 50,  'Hakkar: from 50%, Blood Siphon, a cone 10 yd in front of him'),
(14834, 24328, 0, 9000,  5000, 0, 40, 1.1, 3, 5, 4,  100, 'Hakkar: Corrupted Blood, 5 circles (4 yd) of blood burst round the hero'),
(14834, 24688, 1, 11000, 6000, 0, 14, 1.6, 2, 0, 14, 100, 'Hakkar: Aspect of Venoxis, a poison ring 14 yd round him (safe within 7)');
-- Shadow Bolt Volley 20741 is cast on himself (target 1), but spell_effect_mod turns its
-- targets into a 30 yd frontal cone (targetB 54), so GetAreaReach finds no area, nothing
-- warns, and it hits a hero in front unwarned. Corrupted Blood now carries his range.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 14834 AND `spell_id` = 20741;   -- Shadow Bolt Volley: an unwarned 30 yd frontal cone

-- Ossirian the Unscarred (15339), Ruins of Ahn'Qiraj. War Stomp round him, the sand storms
-- of the ruins at range, and from 50% the Strength of Ossirian (no crystal to check him).
-- Has Charge and War Stomp (25188).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 15339 AND `spell_id` IN (24375, 25160, 25176);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(15339, 24375, 1, 10000, 5000, 0, 14, 1.5, 2, 0, 14, 100, 'Ossirian the Unscarred: War Stomp, a ring 14 yd round him (safe within 7)'),
(15339, 25160, 0, 10000, 6000, 0, 40, 1.2, 3, 4, 5,  100, 'Ossirian the Unscarred: Sand Storm, 4 whirling circles (5 yd) round the hero'),
(15339, 25176, 0, 8000,  3000, 0, 9,  1.9, 1, 0, 10, 50,  'Ossirian the Unscarred: from 50%, Strength of Ossirian, his swing a cone 10 yd in front');
-- War Stomp 25188 reaches 20 yd (radius index 9, no spell_effect_mod), more than the 15 yd
-- a warning circle can show, so StartArea never casts it: a dead row.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 15339 AND `spell_id` = 25188;   -- War Stomp: 20 yd, never cast; the War Stomp ring replaces it


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
