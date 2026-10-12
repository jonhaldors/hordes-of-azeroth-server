DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013012600');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013012600');
-- Add your query below.


-- Vanilla Bonk: the pickup marker, which also marks where an enemy bolt
-- lands, shows no name. A bolt's mark can be selected (enemies must be able
-- to cast at it), so the client drew its name in green over the ground.
UPDATE `creature_template` SET `name` = '' WHERE `entry` = 900111;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
