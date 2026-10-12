DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012220000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012220000');
-- Add your query below.


-- Vanilla Bonk: the in-run currency is Valor, not coins (feature plan,
-- Phase 1). Columns keep their names (coins_per_kill and the like).
UPDATE `bonk_talent` SET `description` = '+25 Valor at the start of a run.' WHERE `id` = 8;
UPDATE `bonk_talent` SET `description` = 'Skip an upgrade choice once per run for a little Valor.' WHERE `id` = 9;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
