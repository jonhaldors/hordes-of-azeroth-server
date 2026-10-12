DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011233000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011233000');
-- Add your query below.


-- Vanilla Bonk: the run master's arena window shows a card per difficulty,
-- with art, a description and the arenas in its pool.
ALTER TABLE `bonk_tier`
    ADD COLUMN `description` VARCHAR(255) NOT NULL DEFAULT '' COMMENT 'shown on its card in the arena window',
    ADD COLUMN `art`         VARCHAR(128) NOT NULL DEFAULT '' COMMENT 'texture on its card, e.g. a loading screen';

UPDATE `bonk_tier` SET
    `description` = 'Wide open zones with room to run. Every stage cleared makes the next one tougher.',
    `art` = 'Interface\\Glues\\LoadingScreens\\LoadScreenEasternKingdom'
WHERE `id` = 1;
UPDATE `bonk_tier` SET
    `description` = 'Raid halls with little room to run and much stronger foes. More Talent Shards drop.',
    `art` = 'Interface\\Glues\\LoadingScreens\\LoadScreenMoltenCore'
WHERE `id` = 2;

ALTER TABLE `bonk_map`
    ADD COLUMN `icon` VARCHAR(64) NOT NULL DEFAULT 'INV_Misc_Map_01' COMMENT 'Interface\\Icons file name, for the arena window';

UPDATE `bonk_map` SET `icon` = 'INV_Misc_Bandana_03'          WHERE `id` = 1;  -- Westfall
UPDATE `bonk_map` SET `icon` = 'Spell_Fire_LavaSpawn'         WHERE `id` = 2;  -- Molten Core
UPDATE `bonk_map` SET `icon` = 'INV_Misc_Head_Centaur_01'     WHERE `id` = 3;  -- The Barrens
UPDATE `bonk_map` SET `icon` = 'Spell_Shadow_RaiseDead'       WHERE `id` = 4;  -- Duskwood
UPDATE `bonk_map` SET `icon` = 'Ability_Mount_JungleTiger'    WHERE `id` = 5;  -- Zul'Gurub
UPDATE `bonk_map` SET `icon` = 'INV_Misc_AhnQirajTrinket_01'  WHERE `id` = 6;  -- Ruins of Ahn'Qiraj


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
