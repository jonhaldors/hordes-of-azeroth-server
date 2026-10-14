DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261014011000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261014011000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: telegraph stand-ins, creatures that wear a telegraph's
-- model (tools/telegraphs/StandIns.cs: displays 18012-18015). The server
-- sizes a creature from its display's addon row, so each gets one, copied
-- from Blizzard's red wisp as the projectiles' are.
-- =============================================================

DELETE FROM `creature_display_info_addon` WHERE `display_id` BETWEEN 18012 AND 18015;
INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
SELECT d.`id`, a.`build`, a.`bounding_radius`, a.`combat_reach`, a.`speed_walk`, a.`speed_run`, a.`gender`, a.`display_id_other_gender`
FROM `creature_display_info_addon` a
JOIN (SELECT 18012 AS `id` UNION SELECT 18013 UNION SELECT 18014 UNION SELECT 18015) d
WHERE a.`display_id` = 16587;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
