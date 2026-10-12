DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013000500');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013000500');
-- Add your query below.


-- Vanilla Bonk: Rough Lands unlocked Duplicator, which left the upgrades
-- tree (20261012210000); it pays Talent Shards instead.
UPDATE `bonk_quest` SET `reward_type` = 1, `reward_value` = 5 WHERE `id` = 9 AND `reward_type` = 5 AND `reward_value` = 8;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
