DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013020000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013020000');
-- Add your query below.


-- Vanilla Bonk: projectiles off for now. A projectile set off from where the
-- server last heard the hero was, behind the hero the player sees while
-- running, and its hits came out of that spot. Without rows every spell
-- lands on its target from the hero again, as before 20261013019000; the
-- table, the creature and the displays stay for the redesign.
DELETE FROM `bonk_projectile`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
