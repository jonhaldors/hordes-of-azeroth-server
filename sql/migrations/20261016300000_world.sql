DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016300000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016300000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk, Phase 3 mini-events: kobold thieves (8), Scourge Invasion
-- (16), demon portal (32) and stampede (64) join the timeline's mini-event
-- slots (bonk_wave.mini_events, bits by BonkMiniEventType). The long walk-to
-- events (shards, portal) only in the 4:45 slot, so they end before the
-- 8:00 mini-boss; the short ones anywhere.
-- =============================================================
UPDATE `bonk_wave` SET `mini_events` = 1 | 2 | 4 | 8 | 64 WHERE `start_sec` = 120;
UPDATE `bonk_wave` SET `mini_events` = 2 | 4 | 8 | 16 | 32 WHERE `start_sec` = 285;
UPDATE `bonk_wave` SET `mini_events` = 1 | 64 WHERE `start_sec` = 420;
UPDATE `bonk_wave` SET `mini_events` = 1 | 2 | 4 | 8 | 64 WHERE `start_sec` = 525;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
