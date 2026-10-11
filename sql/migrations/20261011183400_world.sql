DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011183400');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011183400');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: the run master at camp (GM Island)
-- =============================================================
-- Custom ids start at 900001 in creature_template, creature,
-- npc_text and broadcast_text so upstream data never collides.
-- The gossip menu lists `bonk_map` (script npc_bonk_run_master).

DELETE FROM `broadcast_text` WHERE `entry` = 900001;
INSERT INTO `broadcast_text` (`entry`, `male_text`, `female_text`) VALUES
(900001, 'Back already, $n? Pick an arena and I\'ll send you in.$B$BWin or lose, you wake up right here.',
         'Back already, $n? Pick an arena and I\'ll send you in.$B$BWin or lose, you wake up right here.');

DELETE FROM `npc_text` WHERE `ID` = 900001;
INSERT INTO `npc_text` (`ID`, `BroadcastTextID0`, `Probability0`) VALUES
(900001, 900001, 1);

-- static_flags1 262 = no xp, no loot, sessile.
DELETE FROM `creature_template` WHERE `entry` = 900001;
INSERT INTO `creature_template` (`entry`, `patch`, `name`, `subname`, `level_min`, `level_max`, `faction`, `npc_flags`,
    `display_id1`, `display_probability1`, `display_total_probability`, `type`, `unit_class`, `inhabit_type`,
    `static_flags1`, `script_name`) VALUES
(900001, 0, 'Gizzik Bonkwhistle', 'Run Master', 60, 60, 35, 1,
    7052, 1, 1, 7, 1, 1,
    262, 'npc_bonk_run_master');

-- In front of the camp spawn point, facing it.
DELETE FROM `creature` WHERE `guid` = 900001;
INSERT INTO `creature` (`guid`, `id`, `map`, `position_x`, `position_y`, `position_z`, `orientation`,
    `spawntimesecsmin`, `spawntimesecsmax`, `wander_distance`, `movement_type`) VALUES
(900001, 900001, 1, 16225.6, 16264.5, 13.239, 4.79222, 120, 120, 0, 0);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
