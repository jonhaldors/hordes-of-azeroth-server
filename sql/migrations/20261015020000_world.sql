DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015020000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015020000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: the encircling horde moves into the mini-event scheduler
-- (feature plan, Phase 1 content frameworks)
--
-- Rings no longer come at set times per map: the scheduler (BonkRunEvents)
-- surrounds the hero with a horde every couple of minutes instead,
-- unmarked.
-- =============================================================

DELETE FROM `bonk_map_event` WHERE `event` = 4;
ALTER TABLE `bonk_map_event`
    MODIFY COLUMN `event` TINYINT UNSIGNED NOT NULL COMMENT '0 mini-boss, 1 elites, 2 swarm of normal enemies, 3 chests that cost Valor, 4 ring (now a mini-event), 5 rush from one side';


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
