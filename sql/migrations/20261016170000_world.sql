DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016170000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016170000');
-- Add your query below.


-- Vanilla Bonk: no ammo (user, 2026-10-16). Bows, guns and crossbows fire
-- without it (Spell::CheckItems), so new Hunters no longer start with
-- Darkmoon Arrows; BonkPlayer::OnLogin takes away the ones handed out.
DELETE FROM `playercreateinfo_item` WHERE `itemid` IN (900226, 900227);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
