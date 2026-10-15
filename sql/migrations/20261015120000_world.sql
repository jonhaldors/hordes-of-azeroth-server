DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015120000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015120000');
-- Add your query below.


-- Vanilla Bonk: experience orbs merge up the colours (BonkRun::MergeOrbs):
-- 5 orbs of a colour lying together become one of the next, blue, green,
-- red, purple. Purple is new (900103, display 17999 from the client patch,
-- tools/telegraphs/Wisps.cs). Every colour is the blue wisp recoloured, one
-- size step bigger than the one below; the patch shrinks each colour's
-- model box to match, so all of them cast the blue orb's shadow. Keep these
-- scales and Wisps.cs's in step.
UPDATE `creature_template` SET `display_scale1` = 0.45 WHERE `entry` = 900101;
UPDATE `creature_template` SET `display_scale1` = 0.55 WHERE `entry` = 900102;

-- The server's size row for the new display, as green's and red's.
DELETE FROM `creature_display_info_addon` WHERE `display_id` = 17999;
INSERT INTO `creature_display_info_addon` (`display_id`, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`)
SELECT 17999, `build`, `bounding_radius`, `combat_reach`, `speed_walk`, `speed_run`, `gender`, `display_id_other_gender`
FROM `creature_display_info_addon` WHERE `display_id` = 18001;

DROP TEMPORARY TABLE IF EXISTS `bonk_orb_template`;
CREATE TEMPORARY TABLE `bonk_orb_template` SELECT * FROM `creature_template` WHERE `entry` = 900102;
UPDATE `bonk_orb_template` SET `entry` = 900103, `display_id1` = 17999, `display_scale1` = 0.65;
DELETE FROM `creature_template` WHERE `entry` = 900103;
INSERT INTO `creature_template` SELECT * FROM `bonk_orb_template`;
DROP TEMPORARY TABLE `bonk_orb_template`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
