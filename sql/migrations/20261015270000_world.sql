DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015270000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015270000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: enemies keep their own size
--
-- Rule (2026-10-15): an enemy is never scaled down. Trash comes from a
-- zone's naturally small creatures, bruisers from its big ones, so a
-- tanky enemy reads as tanky at a glance. Only scaling up is allowed
-- (elites, bosses, bruisers grown past the biggest grunt in BonkRun).
-- Creatures whose own size didn't fit their archetype move to the one
-- that fits; a few small ones join as swarmers.
-- Archetypes: 0 swarmer, 1 grunt, 2 bruiser, 3 caster, 4 charger.
-- =============================================================

-- Every roster row, elites and bosses included, at its own size or larger.
UPDATE `bonk_map_creature` SET `scale` = 1 WHERE `scale` < 1;

-- Westfall: the Rusty Harvest Golem was a shrunken golem among the
-- Harvest Golem bruisers; a Murloc Streamrunner takes its place.
DELETE FROM `bonk_map_creature` WHERE `bonk_map_id` = 1 AND `entry` IN (480, 735) AND `kind` = 0;
INSERT INTO `bonk_map_creature` (`bonk_map_id`, `entry`, `kind`, `archetype`, `min_sec`, `max_sec`, `weight`, `max_alive`, `health_multiplier`, `damage_multiplier`, `speed`, `scale`) VALUES
(1, 735, 0, 0, 240, 480, 3, 3, 1, 1, 0.95, 1);

-- Molten Core: Firesworn are no small fry (grunts); Flamewakers are giants (bruisers).
UPDATE `bonk_map_creature` SET `archetype` = 1 WHERE `bonk_map_id` = 2 AND `entry` = 12099 AND `kind` = 0;
UPDATE `bonk_map_creature` SET `archetype` = 2, `min_sec` = 0, `max_sec` = 0 WHERE `bonk_map_id` = 2 AND `entry` = 11661 AND `kind` = 0;

-- The Barrens: a plainstrider stands taller than the swarm (grunt).
UPDATE `bonk_map_creature` SET `archetype` = 1 WHERE `bonk_map_id` = 3 AND `entry` = 3245 AND `kind` = 0;

-- Zul'Gurub: a bat with its rider swoops in (charger); the Venombrood is a big serpent (bruiser).
UPDATE `bonk_map_creature` SET `archetype` = 4, `weight` = 2, `max_alive` = 2 WHERE `bonk_map_id` = 5 AND `entry` = 14750 AND `kind` = 0;
UPDATE `bonk_map_creature` SET `archetype` = 2, `min_sec` = 0, `max_sec` = 0, `max_alive` = 3 WHERE `bonk_map_id` = 5 AND `entry` = 14532 AND `kind` = 0;

-- Ruins of Ahn'Qiraj: the big wasps are grunts, the Shrieker Scarab is swarm,
-- and the Hive'Zara Soldier and Sandstalker are bruiser-sized.
UPDATE `bonk_map_creature` SET `archetype` = 1 WHERE `bonk_map_id` = 6 AND `entry` = 15325 AND `kind` = 0;
UPDATE `bonk_map_creature` SET `archetype` = 0 WHERE `bonk_map_id` = 6 AND `entry` = 15461 AND `kind` = 0;
UPDATE `bonk_map_creature` SET `archetype` = 2, `min_sec` = 0, `max_sec` = 0, `max_alive` = 3 WHERE `bonk_map_id` = 6 AND `entry` IN (15320, 15323) AND `kind` = 0;

-- Tanaris: Hazzali Wasps are grunts; Dunemaul Ogres are as big as their Enforcers (bruisers).
UPDATE `bonk_map_creature` SET `archetype` = 1 WHERE `bonk_map_id` = 7 AND `entry` = 5441 AND `kind` = 0;
UPDATE `bonk_map_creature` SET `archetype` = 2, `min_sec` = 0, `max_sec` = 0 WHERE `bonk_map_id` = 7 AND `entry` = 5471 AND `kind` = 0;

-- Un'Goro Crater: Tar Beasts, Elder Diemetradons and Pterrordaxes are
-- bruiser-sized; Bloodpetal Pests join the swarm.
UPDATE `bonk_map_creature` SET `archetype` = 2, `min_sec` = 0, `max_sec` = 0 WHERE `bonk_map_id` = 8 AND `entry` IN (6517, 9164, 9166) AND `kind` = 0;
DELETE FROM `bonk_map_creature` WHERE `bonk_map_id` = 8 AND `entry` = 9157 AND `kind` = 0;
INSERT INTO `bonk_map_creature` (`bonk_map_id`, `entry`, `kind`, `archetype`, `min_sec`, `max_sec`, `weight`, `max_alive`, `health_multiplier`, `damage_multiplier`, `speed`, `scale`) VALUES
(8, 9157, 0, 0, 0, 240, 6, 0, 1, 1, 0.9, 1);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
