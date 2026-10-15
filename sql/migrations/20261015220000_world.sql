DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015220000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015220000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: Hunter v2 looks the hero can see the moment they appear.
-- The 1.12 client fades in every new object over two seconds, and the
-- first playtest showed neither the thrown traps (game objects lying two
-- seconds) nor Bestial Wrath's beast (alive two seconds).
--
-- Thrown traps are worn by telegraph stand-ins (BonkRun::ThrowTrap):
-- creature displays 17986-17989 from the client patch
-- (tools/telegraphs/Traps.cs), Explosive and Frost Trap, set and sprung,
-- sized like the stand-ins' (16587).
-- Bestial Wrath's beasts wait unseen at the hero's side and only put on
-- the Ghost Wolf look to leap: like every helper, no name, so none shows
-- while it is unseen.
-- =============================================================

DELETE FROM `creature_display_info_addon` WHERE `display_id` BETWEEN 17986 AND 17989;
INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
SELECT d.`id`, a.`build`, a.`bounding_radius`, a.`combat_reach`, a.`speed_walk`, a.`speed_run`, a.`gender`, a.`display_id_other_gender`
FROM `creature_display_info_addon` a
JOIN (SELECT 17986 AS `id` UNION SELECT 17987 UNION SELECT 17988 UNION SELECT 17989) d
WHERE a.`display_id` = 16587;

UPDATE `creature_template` SET `name` = '' WHERE `entry` = 900115;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
