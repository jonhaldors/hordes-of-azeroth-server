DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013011000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013011000');
-- Add your query below.


-- Vanilla Bonk: green orbs are the green wisp (display 18000, our
-- WispGreen.mdx from patch-3.MPQ, built off the red wisp's rows: half size
-- in its display row, so twice the scale for the size green orbs had).
DELETE FROM `creature_display_info_addon` WHERE `display_id` = 18000;
INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
SELECT 18000, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`
FROM `creature_display_info_addon` WHERE `display_id` = 16587;

UPDATE `creature_template` SET `display_id1` = 18000, `display_scale1` = 1.2 WHERE `entry` = 900101;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
