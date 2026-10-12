DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012190000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012190000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: Pickup Marker, an invisible trigger (the Sanguine Pool's
-- copy) that turkey legs and Talent Shards wear a highlight on.
-- =============================================================

DROP TEMPORARY TABLE IF EXISTS `bonk_marker_template`;
CREATE TEMPORARY TABLE `bonk_marker_template` SELECT * FROM `creature_template` WHERE `entry` = 900110 AND `patch` = 0;
UPDATE `bonk_marker_template` SET `entry` = 900111, `name` = 'Pickup Marker';
DELETE FROM `creature_template` WHERE `entry` = 900111;
INSERT INTO `creature_template` SELECT * FROM `bonk_marker_template`;
DROP TEMPORARY TABLE `bonk_marker_template`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
