DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013000000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013000000');
-- Add your query below.


-- Vanilla Bonk: two Fury Mastery nodes were both called Blood Craze.
UPDATE `bonk_mastery` SET `name` = 'Unbridled Wrath' WHERE `id` = 10204;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
