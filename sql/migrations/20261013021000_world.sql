DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013021000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013021000');
-- Add your query below.


-- Vanilla Bonk: run helpers have no name, as the pickup marker (20261013012500).
-- An experience orb is summoned and then made unselectable; for that first
-- update the client had a selectable friendly unit and drew its name in green
-- where the orb appeared. Without a name there is nothing to draw, ever.
-- The Sanguine Pool and the Projectile are the same kind of helper.
UPDATE `creature_template` SET `name` = '' WHERE `entry` IN (900100, 900101, 900102, 900110, 900113);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
