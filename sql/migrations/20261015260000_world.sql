DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015260000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015260000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: no unavoidable enemy damage (user, Barrens playtest).
-- A spell cast at the player with no missile (speed 0) lands at once:
-- BonkMobAI::AimBolt only aims bolts that fly, at a spot under a red
-- circle. At Wall 1 a dozen Stormhides' Lizard Bolts killed the hero in
-- seconds. Every player-targeted, ranged, missile-less ability on a normal
-- enemy goes; the two casters fire a dodgeable bolt instead, at the usual
-- caster numbers (5 sec, 1x a hit, 30 yd).
--
-- Multi-Shot fires 3 arrows, as in vanilla (user: "feels like a machine
-- gun"); 0.7 of a grunt's health an arrow stays.
-- =============================================================

DELETE FROM `bonk_mob_ability` WHERE (`entry`, `spell_id`) IN ((3238, 5401), (12076, 19641), (11667, 19630));

UPDATE `bonk_mob_ability` SET `spell_id` = 9053, `cooldown_ms` = 5000, `damage` = 1, `max_range` = 30,
    `comment` = 'Scorching Elemental: Fireball (was Scorch, which has no missile to dodge)'
WHERE `entry` = 6520 AND `spell_id` = 13878;
UPDATE `bonk_mob_ability` SET `spell_id` = 19452, `cooldown_ms` = 5000, `damage` = 1, `max_range` = 30,
    `comment` = 'Bloodpetal Trapper: Toxic Spit (was Entangling Roots, which has no missile to dodge)'
WHERE `entry` = 6512 AND `spell_id` = 11922;

UPDATE `bonk_weapon` SET `base_targets` = 3, `description` = 'Fires 3 arrows in a cone in front of you.' WHERE `id` = 37;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
