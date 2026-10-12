DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013004000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013004000');
-- Add your query below.


-- Vanilla Bonk: experience orbs and the invisible markers are friendly to
-- everyone, and helpful area spells skip the not-selectable check, so an
-- elite's buff for its allies (Frenzied Command) landed on nearby orbs and
-- made them glow. Area spells now leave them out.
UPDATE `creature_template` SET `immunity_flags` = `immunity_flags` | 1 WHERE `entry` IN (900100, 900101, 900102, 900110, 900111);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
