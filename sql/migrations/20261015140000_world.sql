DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015140000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015140000');
-- Add your query below.


-- Vanilla Bonk: the blue XP orb gets its own wisp from the client patch
-- (display 17998, tools/telegraphs/Wisps.cs), the blue wisp with 20% of its
-- dust sparkles like the other colours: a few hundred stock wisps cost the
-- client frame rate. Its server size row is the stock blue wisp's.
DELETE FROM `creature_display_info_addon` WHERE `display_id` = 17998;
INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
SELECT 17998, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`
FROM `creature_display_info_addon` WHERE `display_id` = 10045;

UPDATE `creature_template` SET `display_id1` = 17998 WHERE `entry` = 900100;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
