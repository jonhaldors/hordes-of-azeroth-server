DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016410000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016410000');
-- Add your query below.


-- Vanilla Bonk: the Berserking power-up (+30% damage dealt, +10% taken)
-- lasted the battleground buff's 60 s; 15 s now, beside Speed's and
-- Restoration's 10 s (user, 2026-10-16).
UPDATE `spell_template` SET `durationIndex` = 8 WHERE `entry` = 23505;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
