DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013005000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013005000');
-- Add your query below.


-- Vanilla Bonk: orb colours come from their models, not from Glowy auras
-- (those are the affixed elites' look). Red orbs are the red wisp
-- (WispRed.mdx, display 16587, half size in its display row, so twice the
-- scale for the same size as before).
UPDATE `creature_template` SET `display_id1` = 16587, `display_scale1` = 1.8 WHERE `entry` = 900102;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
