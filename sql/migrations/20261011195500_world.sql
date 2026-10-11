DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011195500');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011195500');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: spawn distance per map, Speed power-up
-- =============================================================

-- Enemies used to appear 26-36 yards away, which felt too close.
ALTER TABLE `bonk_map`
    ADD COLUMN `spawn_min_distance` FLOAT NOT NULL DEFAULT 38 COMMENT 'enemies appear at least this far from the player' AFTER `max_alive`,
    ADD COLUMN `spawn_max_distance` FLOAT NOT NULL DEFAULT 48 AFTER `spawn_min_distance`;

-- Westfall is wide open; Molten Core's tunnels need them a little closer.
UPDATE `bonk_map` SET `spawn_min_distance` = 40, `spawn_max_distance` = 50 WHERE `id` = 1;
UPDATE `bonk_map` SET `spawn_min_distance` = 32, `spawn_max_distance` = 42 WHERE `id` = 2;

DELETE FROM `gameobject_template` WHERE `entry` = 900006;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `size`) VALUES
(900006, 0, 5, 5931, 'Speed', 1.0);

DELETE FROM `bonk_powerup` WHERE `entry` = 900006;
INSERT INTO `bonk_powerup` (`entry`, `spell_id`, `weight`) VALUES
(900006, 23451, 1);     -- Speed: +100% run speed for 10 sec


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
