DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016340000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016340000');
-- Add your query below.


-- Vanilla Bonk, Phase 3: the kodo caravan (mini-event 7, bit 128) joins the
-- 4:45 slot with the other long walk-to events.
UPDATE `bonk_wave` SET `mini_events` = `mini_events` | 128 WHERE `start_sec` = 285;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
