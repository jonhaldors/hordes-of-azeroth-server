DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016240000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016240000');
-- Add your query below.


-- Vanilla Bonk, Phase F4: starting spots runs learn on each map (BonkRun::
-- LearnStarts): spawn points that are level, open, reachable, well inside
-- the arena and, on an open map, outdoors; up to 15 a map, 80 yards apart.
-- A run starts at one at random once a map has 4. Delete a row to drop a
-- bad spot (it may be learned again only if it passes the tests again).
CREATE TABLE IF NOT EXISTS `bonk_map_start` (
    `id`  INT UNSIGNED NOT NULL AUTO_INCREMENT,
    `map` INT UNSIGNED NOT NULL COMMENT 'bonk_map.id',
    `x`   FLOAT NOT NULL,
    `y`   FLOAT NOT NULL,
    `z`   FLOAT NOT NULL,
    PRIMARY KEY (`id`),
    KEY `map` (`map`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Vanilla Bonk learned starting spots';


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
