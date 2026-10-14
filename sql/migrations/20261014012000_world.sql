DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
DECLARE displayId INT DEFAULT 18012;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261014012000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261014012000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: telegraph stand-ins, creatures that wear the warnings'
-- models (tools/telegraphs/StandIns.cs: displays 18012-18506, every shape's
-- outline and fills in every size, and a line's pieces). The server sizes a
-- creature from its display's addon row, so each gets one, copied from
-- Blizzard's red wisp as the projectiles' are. Replaces the prototype's four
-- rows (20261014011000).
-- =============================================================

DELETE FROM `creature_display_info_addon` WHERE `display_id` BETWEEN 18012 AND 18999;
WHILE displayId <= 18506 DO
    INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
    SELECT displayId, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`
    FROM `creature_display_info_addon` WHERE `display_id` = 16587;
    SET displayId = displayId + 1;
END WHILE;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
