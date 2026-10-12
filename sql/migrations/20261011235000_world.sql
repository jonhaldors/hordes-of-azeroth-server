DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011235000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011235000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: rotating enemy rosters, Vampire Survivors style
--
-- Vampire Survivors runs one wave a minute with only one to three enemy
-- types in it, and swaps them out as the stage goes on; the fast and odd
-- ones come as timed events (bat swarms, closing rings). Here every type
-- used to stay for the rest of the stage, so by minute six a dozen types
-- were mixed together, casters, archers and chargers included.
--
-- Now each type has a window of about four minutes, so three or four
-- types are around at any time. At most one ranged type and one charger or
-- exploder type is active at once, and those carry a cap on how many can
-- be alive together (`max_alive`), which rings, rushes and swarms respect
-- too.
-- =============================================================

ALTER TABLE `bonk_map_creature`
    ADD COLUMN `max_alive` INT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'most of this enemy alive at once, 0 = no limit' AFTER `weight`;

DELETE FROM `bonk_map_creature` WHERE `kind` = 0 AND `bonk_map_id` BETWEEN 1 AND 6;
INSERT INTO `bonk_map_creature` (`bonk_map_id`, `entry`, `kind`, `min_sec`, `max_sec`, `weight`, `max_alive`, `health_multiplier`, `damage_multiplier`, `speed`, `scale`) VALUES
-- Westfall
(1,    38, 0,   0, 210, 10, 0, 1.0, 1.0, 0.70, 1.0),  -- Defias Thug
(1,   199, 0,   0, 180,  6, 0, 0.6, 0.8, 0.85, 1.0),  -- Young Fleshripper
(1,   117, 0,  60, 300,  8, 0, 1.0, 1.0, 0.70, 1.0),  -- Riverpaw Gnoll
(1,   515, 0, 120, 360,  7, 0, 1.0, 1.0, 0.75, 1.0),  -- Murloc Raider
(1,   454, 0, 150, 330,  2, 2, 0.8, 1.0, 0.80, 1.0),  -- Young Goretusk (charges)
(1,   589, 0, 150, 330,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Defias Pillager (caster)
(1,   481, 0, 210, 450,  7, 0, 1.0, 1.0, 0.75, 1.0),  -- Defias Footpad
(1,   480, 0, 240, 480,  3, 3, 0.5, 1.0, 0.95, 0.8),  -- Rusty Harvest Golem (blows up)
(1,    36, 0, 300, 540,  5, 0, 1.6, 1.2, 0.60, 1.0),  -- Harvest Golem
(1,   124, 0, 330,   0,  6, 0, 1.4, 1.2, 0.70, 1.0),  -- Riverpaw Brute
(1,   517, 0, 360, 540,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Murloc Oracle (caster)
(1,   157, 0, 420,   0,  2, 2, 1.2, 1.0, 0.80, 1.0),  -- Goretusk (charges)
(1,   122, 0, 420,   0,  6, 0, 1.3, 1.2, 0.75, 1.0),  -- Defias Highwayman
(1,   115, 0, 480,   0,  4, 0, 2.0, 1.4, 0.65, 1.0),  -- Harvest Reaper
(1,   127, 0, 480,   0,  3, 3, 1.2, 1.2, 0.80, 1.0),  -- Murloc Tidehunter (Frost Nova)

-- Molten Core
(2, 11666, 0,   0, 240,  8, 0, 1.0, 1.0, 0.70, 0.8),  -- Firewalker
(2, 11669, 0,   0, 300,  4, 4, 0.5, 0.8, 0.90, 1.0),  -- Flame Imp (blows up)
(2, 11671, 0,  60, 330,  3, 3, 1.2, 1.0, 0.80, 0.8),  -- Core Hound (charges)
(2, 11667, 0, 120, 360,  7, 0, 1.3, 1.2, 0.70, 0.8),  -- Flameguard
(2, 12265, 0, 150, 360,  3, 3, 0.6, 0.8, 0.75, 0.6),  -- Lava Spawn (caster)
(2, 12076, 0, 240, 480,  6, 0, 1.8, 1.2, 0.60, 0.7),  -- Lava Elemental
(2, 12099, 0, 300, 540,  3, 3, 1.0, 1.0, 0.70, 0.8),  -- Firesworn (erupts)
(2, 11672, 0, 300,   0,  6, 0, 1.6, 1.3, 0.70, 0.7),  -- Core Rager
(2, 12101, 0, 360, 540,  2, 2, 1.2, 1.0, 0.90, 0.7),  -- Lava Surger (charges)
(2, 11668, 0, 420,   0,  5, 0, 2.0, 1.4, 0.70, 0.8),  -- Firelord
(2, 11661, 0, 420,   0,  5, 0, 1.5, 1.3, 0.75, 0.6),  -- Flamewaker
(2, 11658, 0, 480,   0,  3, 4, 3.0, 1.6, 0.55, 0.6),  -- Molten Giant

-- The Barrens
(3,  3266, 0,   0, 210, 10, 0, 1.0, 1.0, 0.70, 1.0),  -- Razormane Defender
(3,  4127, 0,   0, 180,  7, 0, 0.8, 1.0, 0.85, 1.0),  -- Hecklefang Hyena
(3,  3245, 0,  60, 270,  6, 0, 0.6, 0.8, 0.85, 1.0),  -- Ornery Plainstrider
(3,  3272, 0, 120, 360,  8, 0, 1.0, 1.0, 0.70, 1.0),  -- Kolkar Wrangler
(3,  3269, 0, 150, 330,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Razormane Geomancer (caster)
(3,  3256, 0, 210, 450,  7, 0, 0.9, 1.1, 0.85, 1.0),  -- Sunscale Scytheclaw
(3,  3285, 0, 240, 480,  3, 3, 0.5, 1.0, 0.95, 1.0),  -- Venture Co. Peon (blows up)
(3,  3276, 0, 270, 510,  6, 0, 0.9, 1.0, 0.75, 1.0),  -- Witchwing Harpy
(3,  3252, 0, 300, 540,  6, 0, 0.6, 0.9, 0.90, 1.0),  -- Silithid Swarmer
(3,  3258, 0, 360, 510,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Bristleback Hunter (archer)
(3,  3459, 0, 360,   0,  6, 0, 1.3, 1.2, 0.75, 1.0),  -- Razormane Warfrenzy
(3,  7873, 0, 420,   0,  5, 0, 1.5, 1.3, 0.70, 1.0),  -- Razorfen Battleguard
(3,  3238, 0, 480,   0,  4, 0, 2.0, 1.4, 0.65, 1.0),  -- Stormhide
(3,  3273, 0, 510,   0,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Kolkar Stormer (caster)

-- Duskwood
(4,    48, 0,   0, 210, 10, 0, 1.0, 1.0, 0.70, 1.0),  -- Skeletal Warrior
(4,   213, 0,   0, 180,  7, 0, 0.8, 1.0, 0.85, 1.0),  -- Starving Dire Wolf
(4,   539, 0,  60, 270,  6, 0, 0.6, 0.8, 0.85, 1.0),  -- Pygmy Venom Web Spider
(4,     3, 0, 120, 360,  8, 0, 1.0, 1.0, 0.70, 1.0),  -- Flesh Eater
(4,   203, 0, 150, 330,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Skeletal Mage (caster)
(4,   205, 0, 210, 450,  7, 0, 0.9, 1.0, 0.85, 1.0),  -- Nightbane Dark Runner
(4,  1270, 0, 240, 480,  3, 3, 0.5, 1.0, 0.95, 1.0),  -- Fetid Corpse (bursts)
(4,   217, 0, 270, 510,  6, 0, 1.0, 1.0, 0.80, 1.0),  -- Venom Web Spider
(4,   570, 0, 300, 540,  6, 0, 1.2, 1.1, 0.70, 1.0),  -- Brain Eater
(4,   533, 0, 360, 540,  3, 3, 0.8, 1.0, 0.70, 1.0),  -- Nightbane Shadow Weaver (caster)
(4,   628, 0, 360,   0,  6, 0, 1.2, 1.1, 0.85, 1.0),  -- Black Ravager
(4,   206, 0, 420,   0,  2, 2, 1.3, 1.2, 0.80, 1.0),  -- Nightbane Vile Fang (charges)
(4,   202, 0, 420,   0,  5, 0, 1.5, 1.2, 0.70, 1.0),  -- Skeletal Horror
(4,   889, 0, 480,   0,  4, 0, 2.0, 1.4, 0.65, 1.0),  -- Splinter Fist Ogre

-- Zul'Gurub
(5, 11360, 0,   0, 210, 10, 0, 0.6, 0.8, 0.90, 0.8),  -- Zulian Cub
(5, 11368, 0,   0, 180,  6, 0, 0.6, 0.8, 0.95, 0.7),  -- Bloodseeker Bat
(5, 11372, 0,  60, 270,  7, 0, 0.8, 1.0, 0.85, 0.8),  -- Razzashi Adder
(5, 11353, 0, 120, 360,  8, 0, 1.0, 1.0, 0.70, 0.8),  -- Gurubashi Blood Drinker
(5, 11350, 0, 150, 330,  3, 3, 0.8, 1.0, 0.70, 0.8),  -- Gurubashi Axe Thrower (shoots)
(5, 11365, 0, 210, 450,  7, 0, 1.0, 1.0, 0.85, 0.8),  -- Zulian Panther
(5, 14750, 0, 240, 480,  3, 3, 0.5, 1.0, 0.95, 0.7),  -- Gurubashi Bat Rider (blows up)
(5, 14821, 0, 270, 510,  6, 0, 1.1, 1.1, 0.85, 0.8),  -- Razzashi Raptor
(5, 14532, 0, 300, 540,  5, 0, 1.2, 1.1, 0.80, 0.7),  -- Razzashi Venombrood
(5, 15111, 0, 360, 540,  3, 3, 0.8, 1.0, 0.70, 0.8),  -- Mad Servant (caster)
(5, 11351, 0, 360,   0,  6, 0, 1.3, 1.2, 0.75, 0.8),  -- Gurubashi Headhunter
(5, 11361, 0, 420,   0,  5, 0, 1.2, 1.2, 0.85, 0.8),  -- Zulian Tiger
(5, 11356, 0, 420,   0,  2, 2, 1.6, 1.3, 0.70, 0.8),  -- Gurubashi Champion (charges)
(5, 11352, 0, 480,   0,  3, 3, 3.0, 1.6, 0.55, 0.6),  -- Gurubashi Berserker

-- Ruins of Ahn'Qiraj
(6, 15168, 0,   0, 240, 10, 0, 0.5, 0.8, 0.95, 1.0),  -- Vile Scarab
(6, 15325, 0,   0, 180,  7, 0, 0.7, 0.9, 0.90, 0.6),  -- Hive'Zara Wasp
(6, 15318, 0,  60, 300,  8, 0, 1.0, 1.0, 0.75, 0.6),  -- Hive'Zara Drone
(6, 15327, 0, 120, 360,  6, 0, 1.0, 1.0, 0.80, 0.6),  -- Hive'Zara Stinger
(6, 15462, 0, 150, 330,  3, 3, 0.8, 1.0, 0.75, 0.6),  -- Spitting Scarab (spits)
(6, 15461, 0, 210, 450,  5, 0, 0.9, 1.0, 0.80, 0.6),  -- Shrieker Scarab
(6, 15333, 0, 240, 480,  3, 3, 0.5, 1.0, 0.95, 0.8),  -- Silicate Feeder (bursts)
(6, 15387, 0, 270, 510,  7, 0, 1.2, 1.1, 0.75, 0.6),  -- Qiraji Warrior
(6, 15344, 0, 300, 540,  6, 0, 1.0, 1.0, 0.85, 0.6),  -- Swarmguard Needler
(6, 15320, 0, 360,   0,  6, 0, 1.3, 1.2, 0.75, 0.6),  -- Hive'Zara Soldier
(6, 15323, 0, 420,   0,  5, 0, 1.3, 1.2, 0.80, 0.6),  -- Hive'Zara Sandstalker
(6, 15338, 0, 420,   0,  3, 3, 2.0, 1.3, 0.60, 0.5),  -- Obsidian Destroyer
(6, 15324, 0, 480,   0,  4, 4, 1.8, 1.4, 0.65, 0.5),  -- Qiraji Gladiator
(6, 15355, 0, 510,   0,  2, 2, 3.0, 1.6, 0.55, 0.4);  -- Anubisath Guardian

-- Chargers keep their burst, just less often.
UPDATE `bonk_mob_ability` SET `cooldown_ms` = 12000, `first_ms` = 3000 WHERE `spell_id` = 6268 AND `entry` IN (454, 157, 206);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
