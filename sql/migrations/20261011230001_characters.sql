DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011230001');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011230001');
-- Add your query below.


-- Vanilla Bonk: each character's Talent Shards.
CREATE TABLE IF NOT EXISTS `bonk_character_shards` (
    `guid`   INT UNSIGNED NOT NULL,
    `shards` INT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Vanilla Bonk Talent Shards per character';


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
