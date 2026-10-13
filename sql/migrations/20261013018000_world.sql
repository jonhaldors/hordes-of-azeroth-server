DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013018000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013018000');
-- Add your query below.


-- Vanilla Bonk: a gentler start to the spawn curve. The crowd to keep alive
-- grew too fast in the first minutes, while the hero still has one or two
-- spells; it now starts about a quarter lower and meets the old curve again
-- at 10:00. Difficulty still multiplies it (Medium x1.3, Hard x1.6).
UPDATE `bonk_spawn_curve` SET `min_alive` = 20  WHERE `second` = 0;     -- was 25
UPDATE `bonk_spawn_curve` SET `min_alive` = 38  WHERE `second` = 120;   -- was 50
UPDATE `bonk_spawn_curve` SET `min_alive` = 58  WHERE `second` = 240;   -- was 75
UPDATE `bonk_spawn_curve` SET `min_alive` = 85  WHERE `second` = 360;   -- was 100
UPDATE `bonk_spawn_curve` SET `min_alive` = 120 WHERE `second` = 480;   -- was 130


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
