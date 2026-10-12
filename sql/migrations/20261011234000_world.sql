DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011234000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011234000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: gentler starts, sustain for every class
-- =============================================================

-- Enemy hits ramp up through a stage instead of starting at full strength.
ALTER TABLE `bonk_map`
    ADD COLUMN `damage_growth_per_min` FLOAT NOT NULL DEFAULT 0 COMMENT 'enemy damage grows this share per minute of the stage' AFTER `health_growth_per_min`;

-- About 70% of the old damage at the start, 120% by the stage boss.
UPDATE `bonk_map` SET `damage_multiplier` = 0.25, `damage_growth_per_min` = 0.07 WHERE `id` IN (1, 3);   -- Westfall, The Barrens
UPDATE `bonk_map` SET `damage_multiplier` = 0.27, `damage_growth_per_min` = 0.07 WHERE `id` = 4;         -- Duskwood
UPDATE `bonk_map` SET `damage_multiplier` = 0.45, `damage_growth_per_min` = 0.07 WHERE `id` IN (2, 5, 6); -- Molten Core, Zul'Gurub, Ruins of Ahn'Qiraj

-- Healing weapons heal a share of max health, so they keep pace with the
-- player's level instead of a fixed amount.
ALTER TABLE `bonk_weapon`
    ADD COLUMN `heal_pct` FLOAT NOT NULL DEFAULT 0 COMMENT 'heals the player this % of max health per cast' AFTER `evolve_passive`;

-- Holy Nova: the priest spell healed a flat ~52 every cast from its own
-- script, which carried a low-level priest through anything. The creature
-- version has the same look and no script; its area is 15 yards instead of
-- 10, so it hits a little softer.
UPDATE `bonk_weapon` SET `spell_id` = 20694, `range` = 15, `damage_fraction` = 0.3, `heal_pct` = 2.5,
    `description` = 'Burst of holy light around you that also heals you.' WHERE `id` = 5;

-- Leech: any class can sustain itself by dealing damage.
DELETE FROM `bonk_passive` WHERE `id` = 10;
INSERT INTO `bonk_passive` (`id`, `name`, `icon`, `description`, `effect`, `value_per_rank`, `max_rank`) VALUES
(10, 'Leech', 'Spell_Shadow_LifeDrain02', 'Heal for 2% of the damage your weapons deal.', 9, 2, 5);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
