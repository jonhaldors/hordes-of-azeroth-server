DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016420000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016420000');
-- Add your query below.


-- Vanilla Bonk: the Extraction Vault showed nothing (display 1287 is the
-- invisible trap model). The Dark Iron Safe from Blackrock Depths.
UPDATE `gameobject_template` SET `displayId` = 3014, `size` = 1 WHERE `entry` = 900041;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
