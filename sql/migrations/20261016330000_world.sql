DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016330000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016330000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk, Phase 3 bosses: signature moves (bonk_mob_ability.shape,
-- BonkMobAI::StartShape): 1 a frontal cone `size` long, 2 a ring round the
-- enemy `size` across (safe within half), 3 a barrage of `count` circles
-- `size` wide round the hero, 4 a trail of `count` of the stage's enemies.
-- The spell only names it; `damage` in melee hits; `below_health` holds it
-- back until the enemy is below that % of its health.
-- First, the Normal bosses from the plan's boss roster: Edwin VanCleef's
-- shadow dashes and cannon barrage, Mr. Smite's stomp and charges, Stitches'
-- cleave and his ghouls; Charlga Razorflank (the Barrens' boss now, not
-- Mutanus) a thorn ring.
-- =============================================================
ALTER TABLE `bonk_mob_ability`
    ADD COLUMN IF NOT EXISTS `shape` TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '0 spell, 1 cone, 2 ring, 3 barrage, 4 trail' AFTER `damage`,
    ADD COLUMN IF NOT EXISTS `count` TINYINT UNSIGNED NOT NULL DEFAULT 0 AFTER `shape`,
    ADD COLUMN IF NOT EXISTS `size` FLOAT NOT NULL DEFAULT 0 AFTER `count`,
    ADD COLUMN IF NOT EXISTS `below_health` FLOAT NOT NULL DEFAULT 100 AFTER `size`;

-- Edwin VanCleef: shadow dashes (a Charge: red line, then the dash), and
-- from 66% a cannon barrage from the ships.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 639 AND `spell_id` IN (22911, 7978);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(639, 22911, 0, 10000, 4000, 8, 35, 1.5, 0, 0, 0, 100, 'Edwin VanCleef: shadow dash (Charge: a red line, then the dash)'),
(639, 7978,  0, 9000,  6000, 0, 40, 1.0, 3, 5, 4, 66,  'Edwin VanCleef: cannon barrage from 66%, 5 circles round the hero');

-- Mr. Smite: a stomp (ring round him) and from 50% he charges.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 646;
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(646, 6432,  1, 9000,  3000, 0, 8,  1.2, 2, 0, 8, 100, 'Mr. Smite: Smite Stomp, a ring round him (safe up close)'),
(646, 22911, 0, 8000,  1000, 8, 30, 1.5, 0, 0, 0, 50,  'Mr. Smite: from 50%, axes out: a charge along a red line');

-- Stitches: a cleave in front, and ghouls rising as he lumbers on.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 412 AND `spell_id` IN (15496, 17616);
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(412, 15496, 0, 6000,  3000, 0, 7,  1.5, 1, 0, 8, 100, 'Stitches: Cleave, a cone in front'),
(412, 17616, 1, 9000,  5000, 0, 60, 0,   4, 2, 0, 100, 'Stitches: ghouls rise in his wake (2 of the stage''s enemies)');

-- Charlga Razorflank: a ring of thorns round her (safe close in).
DELETE FROM `bonk_mob_ability` WHERE `entry` = 4421 AND `spell_id` = 8148;
INSERT INTO `bonk_mob_ability` (`entry`, `spell_id`, `target`, `cooldown_ms`, `first_ms`, `min_range`, `max_range`, `damage`, `shape`, `count`, `size`, `below_health`, `comment`) VALUES
(4421, 8148, 1, 11000, 7000, 0, 12, 1.5, 2, 0, 12, 100, 'Charlga Razorflank: a ring of thorns (safe within 6 yd)');


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
