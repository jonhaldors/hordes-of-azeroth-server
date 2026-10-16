DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016190000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016190000');
-- Add your query below.


-- Vanilla Bonk: Volley no longer channels (user, 2026-10-16: no channel bar,
-- the area lasts its whole time). As a channel it showed the hero a channel
-- bar, a jump cancelled it, and each new Volley (another drop of the same
-- cast too) ended the last one's area: SpellCaster::SetCurrentCastedSpell
-- interrupts the running channel and Spell::SendChannelUpdate(0) removes
-- every area of the spell. Its auras' ticks also hang on the channel
-- (Unit::UpdateAuras leaves channeled holders to the cast), so the flag goes
-- from the spell itself, as in the run's Rain of Fire (19717) and Blizzard
-- (26607): the area then lasts its duration and ticks on its own. 22908 is an
-- NPC copy; besides the Volley weapon only Raene Wolfrunner (3691) and
-- Illyanna Ravenoak (11488) cast it, neither in a run.
UPDATE `spell_template` SET `attributesEx` = `attributesEx` & ~4 WHERE `entry` = 22908;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
