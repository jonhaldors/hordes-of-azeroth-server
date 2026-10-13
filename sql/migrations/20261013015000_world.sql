DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261013015000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261013015000');
-- Add your query below.


-- Vanilla Bonk: Auto Shot is the hero's own auto attack, not a spell; the
-- v2 Hunter kit (docs/Spells.md) drops it and its evolution Rapid Fire.
-- No quest, mastery node or saved data refers to either.
DELETE FROM `bonk_weapon` WHERE `id` IN (200, 210);

-- Frost Trap and Freezing Trap went off with spell 12737, an NPC Frostbolt
-- with a missile: the client drew a frostbolt flying from the hero to every
-- enemy the trap hit. The traps now deal no damage (until the v2 ice field):
-- they go off with Frost Trap Aura instead, the slowing ice patch Frost
-- Trap already left, and Freezing Trap still roots (alt 12494).
UPDATE `bonk_weapon` SET `spell_id` = 13810, `alt_spell_id` = 0 WHERE `id` = 204; -- Frost Trap: hit + patch -> patch
UPDATE `bonk_weapon` SET `spell_id` = 13810 WHERE `id` = 214; -- Freezing Trap: hit + root -> patch + root


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
