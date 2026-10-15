DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015050000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015050000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: the wandering vendor (feature plan, Phase 3)
--
-- Zixil, Merchant Supreme (the goblin who wanders the Hillsbrad road in the
-- real game), summoned at a spawn point once or twice a stage
-- (BonkRunVendor). Clicked, npc_bonk_vendor shows his shelf in the addon;
-- no gossip menu.
-- static_flags1 262 = no xp, no loot, sessile.
-- =============================================================

DELETE FROM `creature_template` WHERE `entry` = 900010;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
    `display_id1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `inhabit_type`,
    `static_flags1`, `script_name`) VALUES
(900010, 0, 'Zixil', 'Merchant Supreme', 60, 60, 35, 1,
    7133, 1, 1, 7, 1, 1,
    262, 'npc_bonk_vendor');


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
