DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015290000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015290000');
-- Add your query below.


-- Vanilla Bonk: the camp mailbox spawns again. 20261012070000 turned it off
-- with gameobject.spawn_flags bit 2 (bonk_hidden_gameobject only records
-- which spawns it turned off); 20261015280000 dropped the record but left
-- the flag on.
UPDATE `gameobject` SET `spawn_flags` = `spawn_flags` & ~2 WHERE `guid` = 230922;
DELETE FROM `bonk_hidden_gameobject` WHERE `guid` = 230922;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
