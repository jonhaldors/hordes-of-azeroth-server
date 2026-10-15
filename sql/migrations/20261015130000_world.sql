DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015130000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015130000');
-- Add your query below.


-- Vanilla Bonk: ground spells aim at most 20 yards from the hero (decided
-- Oct 14, after the stress test: Blizzard aimed 28 yards out with a 15 yard
-- circle killed enemies 40 yards away). Their area is their own; this is
-- only where they can be placed.
UPDATE `bonk_weapon` SET `range` = 20 WHERE `target` = 3 AND `range` > 20;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
