DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015060000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015060000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: more mini-events (feature plan, Phase 3)
--
-- The treasure goblin's looks (BonkRunEvents). The run sets its level,
-- health, faction and speed like any enemy's (BonkRun::SpawnMobAt); the
-- bounty is one of the map's own elites.
-- =============================================================

DELETE FROM `creature_template` WHERE `entry` = 900011;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
    `display_id1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `inhabit_type`,
    `static_flags1`, `script_name`) VALUES
(900011, 0, 'Treasure Goblin', '', 1, 1, 14, 0,
    7163, 1, 1, 7, 1, 1,
    0, '');


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
