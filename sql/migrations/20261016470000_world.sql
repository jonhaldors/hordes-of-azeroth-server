DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016470000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016470000');
-- Add your query below.


-- Vanilla Bonk: a quest that unlocks a spell belongs to the spell's class
-- (user, 2026-10-16: Starfall came from reaching level 20 as anyone). Only
-- that class's runs count, and the quest shows with the class. The generic
-- goals are harder now that they need the class; the spell-kill ones only
-- gain the class column (only that class has the spell anyway).
-- Generic goals, now as the class:
UPDATE `bonk_quest` SET `class` = 11, `amount` = 40, `description` = 'Reach level 40 in a single run as a druid.' WHERE `id` = 32;    -- Starfall
UPDATE `bonk_quest` SET `class` = 7,  `amount` = 40, `description` = 'Reach level 40 in a single run as a shaman.' WHERE `id` = 11;    -- Chain Lightning
UPDATE `bonk_quest` SET `class` = 2,  `amount` = 40, `description` = 'Kill 40 elites as a paladin.' WHERE `id` = 4;                    -- Hammer of Wrath
UPDATE `bonk_quest` SET `class` = 4,  `amount` = 25, `description` = 'Kill 25 elites as a rogue.' WHERE `id` = 34;                     -- Poison Cloud
UPDATE `bonk_quest` SET `class` = 8,  `description` = 'Win a run on Normal as a mage.' WHERE `id` = 7;                                -- Frost Nova
UPDATE `bonk_quest` SET `class` = 9,  `description` = 'Clear stage 1 on Mythic as a warlock.' WHERE `id` = 10;                       -- Hellfire
-- Kills with a class's own spell:
UPDATE `bonk_quest` SET `class` = 1  WHERE `id` = 40;    -- Cleave: Whirlwind
UPDATE `bonk_quest` SET `class` = 2  WHERE `id` = 43;    -- Crusader Strike: Holy Shock
UPDATE `bonk_quest` SET `class` = 3  WHERE `id` = 42;    -- Multi-Shot: Serpent Sting
UPDATE `bonk_quest` SET `class` = 4  WHERE `id` = 41;    -- Sinister Strike: Eviscerate
UPDATE `bonk_quest` SET `class` = 5  WHERE `id` = 44;    -- Shadow Word: Pain: Mind Blast
UPDATE `bonk_quest` SET `class` = 7  WHERE `id` IN (33, 45);    -- Lightning Bolt: Lightning Cloud; Flame Shock: Stormstrike
UPDATE `bonk_quest` SET `class` = 8  WHERE `id` IN (30, 46);    -- Frostbolt: Blizzard; Fireball: Scorch
UPDATE `bonk_quest` SET `class` = 9  WHERE `id` IN (31, 47);    -- Shadow Bolt: Rain of Fire; Corruption: Searing Pain
UPDATE `bonk_quest` SET `class` = 11 WHERE `id` = 48;    -- Insect Swarm: Entangling Roots


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
