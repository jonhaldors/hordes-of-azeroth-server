DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012130000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012130000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: the stat list (feature plan, Phase 1)
--
-- New stats: Hit, Crit, Crit Damage, Shield and Luck; Haste becomes a rate
-- (interval / (1 + Haste)). Defense and the flat Armor merge into one
-- Armor %: gear and Mastery that had flat Armor get the same amount as %.
-- Every source adds into one total per stat (passive effect ids):
--   0 Power, 1 Haste, 2 projectiles, 3 movement speed, 4 max health,
--   5 regen, 6 gold, 7 experience, 8 pickup range, 9 leech, 10 area,
--   11 duration, 12 Armor, 13 Hit, 14 Dodge, 15 Crit, 16 Crit Damage,
--   17 Shield (% of max health), 18 Luck.
-- =============================================================

-- Elites dodge the hero's hits by difficulty; mini-bosses and bosses 5% more.
ALTER TABLE `bonk_tier`
    ADD COLUMN `elite_dodge` FLOAT NOT NULL DEFAULT 0 COMMENT 'chance elites dodge the hero''s hits, %; mini-bosses and bosses dodge 5 more; Hit takes it off' AFTER `mastery_xp_boss`;
UPDATE `bonk_tier` SET `elite_dodge` = 5 WHERE `key` = 'easy';
UPDATE `bonk_tier` SET `elite_dodge` = 10 WHERE `key` = 'medium';
UPDATE `bonk_tier` SET `elite_dodge` = 15 WHERE `key` = 'hard';

-- Gear: Defense and flat Armor become one Armor %, and every new stat gets a column.
UPDATE `bonk_item_stat` SET `defense` = `defense` + `armor`;
ALTER TABLE `bonk_item_stat`
    DROP COLUMN `armor`,
    CHANGE COLUMN `defense` `armor` FLOAT NOT NULL DEFAULT 0 COMMENT 'less damage taken in a run, %',
    MODIFY COLUMN `power` FLOAT NOT NULL DEFAULT 0 COMMENT 'more damage dealt in a run, %',
    ADD COLUMN `hit` FLOAT NOT NULL DEFAULT 0 COMMENT 'less elite and boss dodge, %' AFTER `dodge`,
    ADD COLUMN `crit` FLOAT NOT NULL DEFAULT 0 COMMENT 'crit chance, %' AFTER `hit`,
    ADD COLUMN `crit_damage` FLOAT NOT NULL DEFAULT 0 COMMENT 'bigger crits, %' AFTER `crit`,
    ADD COLUMN `shield` FLOAT NOT NULL DEFAULT 0 COMMENT 'absorb before health, % of max health' AFTER `crit_damage`,
    ADD COLUMN `haste` FLOAT NOT NULL DEFAULT 0 COMMENT 'spells fire and tick more often, %' AFTER `shield`,
    ADD COLUMN `area` FLOAT NOT NULL DEFAULT 0 COMMENT 'bigger area spells and reach, %' AFTER `haste`,
    ADD COLUMN `pickup_range` FLOAT NOT NULL DEFAULT 0 COMMENT 'experience orbs and turkey legs from further away, %' AFTER `area`,
    ADD COLUMN `luck` FLOAT NOT NULL DEFAULT 0 COMMENT 'better rarity, points' AFTER `pickup_range`;

-- Necks crit, rings hit, cloaks reach further for pickups, trinkets each do
-- their own thing; belts and boots shield, bracers widen areas, gloves
-- sharpen crits. The two epic sets lean on their role.
UPDATE `bonk_item_stat` SET `crit` = 1 WHERE `entry` = 900101;          -- Scrapper's Pendant
UPDATE `bonk_item_stat` SET `crit` = 2 WHERE `entry` = 900102;          -- Veteran's Medallion
UPDATE `bonk_item_stat` SET `crit` = 3 WHERE `entry` = 900103;          -- Champion's Talisman
UPDATE `bonk_item_stat` SET `hit` = 2 WHERE `entry` = 900104;           -- Scrapper's Band
UPDATE `bonk_item_stat` SET `hit` = 4 WHERE `entry` = 900105;           -- Veteran's Signet
UPDATE `bonk_item_stat` SET `hit` = 6 WHERE `entry` = 900106;           -- Champion's Loop
UPDATE `bonk_item_stat` SET `luck` = 5 WHERE `entry` = 900107;          -- Lucky Charm
UPDATE `bonk_item_stat` SET `haste` = 3 WHERE `entry` = 900108;         -- Battle Token
UPDATE `bonk_item_stat` SET `crit_damage` = 15 WHERE `entry` = 900109;  -- Idol of Fury
UPDATE `bonk_item_stat` SET `pickup_range` = 10 WHERE `entry` = 900110; -- Scrapper's Cape
UPDATE `bonk_item_stat` SET `pickup_range` = 20 WHERE `entry` = 900111; -- Veteran's Cloak
UPDATE `bonk_item_stat` SET `pickup_range` = 30 WHERE `entry` = 900112; -- Champion's Mantle
UPDATE `bonk_item_stat` SET `shield` = 2 WHERE `entry` IN (900113, 900114);         -- Recruit's Sash, Sandals
UPDATE `bonk_item_stat` SET `area` = 4 WHERE `entry` = 900115;                      -- Recruit's Bracers
UPDATE `bonk_item_stat` SET `crit_damage` = 5 WHERE `entry` = 900116;               -- Recruit's Gloves
UPDATE `bonk_item_stat` SET `shield` = 3 WHERE `entry` IN (900117, 900118);         -- Warden's Girdle, Boots
UPDATE `bonk_item_stat` SET `area` = 6 WHERE `entry` = 900119;                      -- Warden's Bracers
UPDATE `bonk_item_stat` SET `crit_damage` = 10 WHERE `entry` = 900120;              -- Warden's Gauntlets
UPDATE `bonk_item_stat` SET `shield` = 5 WHERE `entry` IN (900121, 900122);         -- Juggernaut Belt, Sabatons
UPDATE `bonk_item_stat` SET `area` = 10 WHERE `entry` = 900123;                     -- Juggernaut Bracers
UPDATE `bonk_item_stat` SET `crit_damage` = 15 WHERE `entry` = 900124;              -- Juggernaut Gauntlets
UPDATE `bonk_item_stat` SET `shield` = 3 WHERE `entry` BETWEEN 900125 AND 900128;   -- Battlegear of the Unbroken
UPDATE `bonk_item_stat` SET `crit` = 2 WHERE `entry` BETWEEN 900129 AND 900132;     -- Regalia of Slaughter

-- Set bonuses: Defense is Armor now, and the cooldown bonus reads as Haste.
UPDATE `bonk_item_set_bonus` SET `passive_effect` = 12 WHERE `passive_effect` = 13;
ALTER TABLE `bonk_item_set_bonus`
    CHANGE COLUMN `defense` `armor` FLOAT NOT NULL DEFAULT 0 COMMENT 'less damage taken in a run, %';
UPDATE `bonk_item_set_bonus` SET `description` = '+10% Armor, and regain 1% health every second.' WHERE `set_id` = 1 AND `pieces` = 4;
UPDATE `bonk_item_set_bonus` SET `description` = '+8% Haste.' WHERE `set_id` = 2 AND `pieces` = 2;

-- Level-up passives: flat Armor is gone (Toughness is the Armor pick), and
-- Crit, Shield and Luck join the pool under their WoW names.
DELETE FROM `bonk_passive` WHERE `id` = 14;
UPDATE `bonk_passive` SET `description` = '+8% Haste: spells fire and tick 8% more often.' WHERE `id` = 2;
UPDATE `bonk_passive` SET `description` = '+4% Armor: take 4% less damage.' WHERE `id` = 13;
UPDATE `bonk_passive` SET `description` = '+30% pickup range for experience orbs and turkey legs.' WHERE `id` = 9;
INSERT INTO `bonk_passive` (`id`, `name`, `icon`, `description`, `effect`, `value_per_rank`, `max_rank`) VALUES
(16, 'Elixir of the Mongoose', 'INV_Potion_32',         '+4% Crit.',                                      15, 4, 5),
(17, 'Inner Fire',             'Spell_Holy_InnerFire',  '+6% of your maximum health as a Shield.',        17, 6, 5),
(18, 'Lucky Rabbit''s Foot',   'INV_Misc_Foot_Kodo',    '+5 Luck: better rarity on chests and level ups.', 18, 5, 5);

-- Camp upgrades for the new stats (the full tree rebuild comes later).
UPDATE `bonk_upgrade` SET `description` = '+4% Haste: spells fire and tick 4% more often.' WHERE `id` = 2;
INSERT INTO `bonk_upgrade` (`id`, `name`, `description`, `effect`, `value_per_rank`, `max_rank`, `cost`, `cost_growth`, `icon`, `tree`, `tree_row`, `tree_column`, `required_id`, `required_rank`) VALUES
(9,  'Crit',         '+1.5% Crit.',                                       15, 1.5, 5, 6500, 1, 'Ability_CriticalStrike',     1, 2, 1, 0, 0),
(10, 'Area',         '+4% area: bigger area spells and melee reach.',     10, 4,   5, 5500, 1, 'Spell_Holy_ArcaneIntellect', 1, 2, 3, 0, 0),
(11, 'Hit',          '+2% Hit: elites and bosses dodge 2% less.',         13, 2,   3, 9000, 1, 'Ability_Marksmanship',       1, 2, 4, 0, 0),
(12, 'Armor',        '+2% Armor: take 2% less damage.',                   12, 2,   5, 5500, 1, 'Spell_Holy_DevotionAura',    2, 1, 1, 0, 0),
(13, 'Shield',       '+3% of your maximum health as a Shield.',           17, 3,   5, 9000, 1, 'Spell_Holy_PowerWordShield', 2, 2, 3, 0, 0),
(14, 'Pickup Range', '+10% pickup range for experience orbs and turkey legs.', 8, 10, 5, 3500, 1, 'INV_Misc_Gem_Pearl_04',  3, 2, 2, 0, 0),
(15, 'Luck',         '+3 Luck: better rarity on chests and level ups.',   18, 3,   5, 9000, 1, 'INV_Misc_Foot_Kodo',         3, 2, 3, 0, 0);

-- Class Mastery nodes that gave flat Armor give Armor % now; Improved Power
-- Word: Shield gives Shield.
UPDATE `bonk_mastery` SET `target` = 17, `value_per_rank` = 3, `description` = '+3% of your maximum health as a Shield.'
    WHERE `effect` = 0 AND `target` = 13 AND `id` = 50103;
UPDATE `bonk_mastery` SET `target` = 12, `value_per_rank` = 2, `description` = '-2% damage taken.'
    WHERE `effect` = 0 AND `target` = 13;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
