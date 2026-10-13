DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013017000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013017000');
-- Add your query below.


-- Vanilla Bonk: enemy health retune (docs/Spells.md, Ranks: "enemy health
-- growth gets retuned once the keyword system is in"). Bruisers, from 2:00,
-- had five times a grunt's health; four is still clearly tanky. The rest of
-- the retune is in the core: no armor on run enemies, and no more health
-- per player level (time and crowd size carry the pressure).
UPDATE `bonk_archetype` SET `health_multiplier` = 4 WHERE `id` = 2; -- Bruiser: 5 -> 4


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
