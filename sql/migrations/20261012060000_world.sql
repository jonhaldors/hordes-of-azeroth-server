DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012060000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012060000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: Megabonk-style enemy scaling
--
-- Enemy health and damage now grow with the run's clock instead of the
-- stage's, so stage 2 starts where stage 1 ended (spawns too), and health
-- also grows 10% per player level (in the core). With the clock no longer
-- resetting, the per-minute growth and the per-stage steps are smaller.
--
-- health = level health * tier/stage/map/creature/kind multipliers
--          * (1 + 0.10 * (player level - 1))
--          * (1 + health_growth_per_min * run minutes)
-- damage = level melee damage * multipliers
--          * (1 + damage_growth_per_min * run minutes)
-- stage multiplier = 1 + stage_*_growth * (stage - 1)
-- =============================================================

-- Easy maps: Westfall, The Barrens, Duskwood
UPDATE `bonk_map` SET `health_growth_per_min` = 0.05, `damage_growth_per_min` = 0.05 WHERE `id` IN (1, 3, 4);
-- Medium maps: Tanaris, Un'Goro Crater, Stranglethorn Vale
UPDATE `bonk_map` SET `health_growth_per_min` = 0.06, `damage_growth_per_min` = 0.055 WHERE `id` IN (7, 8, 9);
-- Hard maps: Molten Core, Zul'Gurub, Ruins of Ahn'Qiraj
UPDATE `bonk_map` SET `health_growth_per_min` = 0.07, `damage_growth_per_min` = 0.06 WHERE `id` IN (2, 5, 6);

UPDATE `bonk_tier` SET `stage_health_growth` = 0.15, `stage_damage_growth` = 0.10 WHERE `key` = 'easy';
UPDATE `bonk_tier` SET `stage_health_growth` = 0.18, `stage_damage_growth` = 0.11 WHERE `key` = 'medium';
UPDATE `bonk_tier` SET `stage_health_growth` = 0.20, `stage_damage_growth` = 0.12 WHERE `key` = 'hard';


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
