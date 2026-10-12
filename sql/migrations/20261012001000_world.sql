DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012001000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012001000');
-- Add your query below.


-- Vanilla Bonk: softer bosses and elites.
-- Rushing Charge (a speed and damage burst) came too often, and too soon
-- after the enemy appeared: Lupos had it every 9 seconds from 2 seconds in.
UPDATE `bonk_mob_ability` SET `cooldown_ms` = 20000, `first_ms` = 8000
WHERE `spell_id` = 6268 AND `entry` IN (521, 730, 731, 1713, 3395, 3475, 6581, 15067);   -- elites and mini-bosses
UPDATE `bonk_mob_ability` SET `cooldown_ms` = 15000, `first_ms` = 6000
WHERE `spell_id` = 6268 AND `entry` IN (157, 206, 454, 1114, 5427, 6513);                -- normal enemies

-- Mr. Smite's Smite Stomp stunned for 10 seconds. Runs make the player
-- immune to stuns now, which leaves it doing nothing; he fights with his
-- hammers instead.
DELETE FROM `bonk_mob_ability` WHERE `entry` = 646 AND `spell_id` = 6432;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
