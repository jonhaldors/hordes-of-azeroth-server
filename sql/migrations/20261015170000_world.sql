DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015170000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015170000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: core balance, step 2 (feature plan, Balance spec)
--
-- Leech returns Megabonk-style as Vampiric Embrace: each hit has a 5% a
-- rank chance to heal 1% of max health, at most 2% a second (BonkRun).
--
-- Sayge's run charges are gated Megabonk-style by quests done (quest type
-- 17, milestones whose reward is the talent, so the existing quest lock
-- holds them), and cost more Darkmoon Tickets:
--   Second Thoughts (Reroll)  10 quests   5 / 10 / 15
--   Moving On (Skip)          15 quests   5 / 10 / 15
--   Banishment (Banish)       25 quests   8 / 12 / 16
--   Second Wind (revive)      a stage boss killed   15
-- =============================================================

DELETE FROM `bonk_passive` WHERE `id` = 20;
INSERT INTO `bonk_passive` (`id`, `name`, `icon`, `description`, `effect`, `value_per_rank`, `max_rank`, `excludes`) VALUES
(20, 'Vampiric Embrace', 'Spell_Shadow_UnsummonBuilding', 'Each hit has a 5% chance to heal 1% of your maximum health (at most 2% a second).', 9, 5, 5, 0);

UPDATE `bonk_talent` SET `cost` = 5,  `cost_growth` = 1.0 WHERE `id` = 5;     -- Second Thoughts
UPDATE `bonk_talent` SET `cost` = 5,  `cost_growth` = 1.0 WHERE `id` = 9;     -- Moving On
UPDATE `bonk_talent` SET `cost` = 8,  `cost_growth` = 0.5 WHERE `id` = 6;     -- Banishment
UPDATE `bonk_talent` SET `cost` = 15, `cost_growth` = 0   WHERE `id` = 7;     -- Second Wind

DELETE FROM `bonk_quest` WHERE `id` BETWEEN 208 AND 211;
INSERT INTO `bonk_quest` (`id`, `name`, `description`, `icon`, `type`, `target`, `param`, `amount`, `reward_type`, `reward_value`, `class`, `category`) VALUES
(208, 'Errand Runner',      'Complete 10 quests.',  'INV_Misc_Note_01',           17, 0, 0, 10, 4, 5, 0, 5),
(209, 'Trusted Adventurer', 'Complete 15 quests.',  'INV_Scroll_03',              17, 0, 0, 15, 4, 9, 0, 5),
(210, 'Renowned',           'Complete 25 quests.',  'INV_Misc_Book_09',           17, 0, 0, 25, 4, 6, 0, 5),
(211, 'Giant Slayer',       'Defeat a stage boss.', 'INV_Misc_Head_Dragon_01',    1,  2, 0, 1,  4, 7, 0, 0);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
