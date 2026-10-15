DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015200000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015200000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: the summon framework (feature plan, Phase 1; docs/Spells.md,
-- Summons and Hunter pet families)
--
-- Call Pet summons the hero's pet family. The Cat is the default and, for
-- the Hunter v2 playtest, the only one: fast bites that apply Bleed. The
-- Scorpid, Core Hound, Gorilla and Spirit Beast come after the playtest,
-- with the Stable Master. A family keeps its spell's damage a second
-- (attack_ms only spreads it over more or fewer bites).
-- =============================================================

DROP TABLE IF EXISTS `bonk_pet_family`;
CREATE TABLE `bonk_pet_family` (
    `id`           INT UNSIGNED     NOT NULL,
    `name`         VARCHAR(32)      NOT NULL,
    `entry`        INT UNSIGNED     NOT NULL COMMENT 'its creature',
    `scale`        FLOAT            NOT NULL DEFAULT 1 COMMENT 'its size before summon size and Area',
    `attack_ms`    INT UNSIGNED     NOT NULL DEFAULT 1500 COMMENT 'between bites, before Haste',
    `applies`      TINYINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'bonk_keyword put on every enemy it bites',
    `apply_stacks` TINYINT UNSIGNED NOT NULL DEFAULT 0,
    PRIMARY KEY (`id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Vanilla Bonk hunter pet families';

-- The Cat: Stranglethorn Tiger's look (display 320), a little smaller than
-- the wolf Call Pet used to call. Its stats come from that wolf (900112).
DELETE FROM `creature_template` WHERE `entry` = 900114;
DROP TEMPORARY TABLE IF EXISTS `bonk_cat`;
CREATE TEMPORARY TABLE `bonk_cat` AS SELECT * FROM `creature_template` WHERE `entry` = 900112;
UPDATE `bonk_cat` SET `entry` = 900114, `name` = 'Cat', `display_id1` = 320, `display_scale1` = 1, `pet_family` = 2;
INSERT INTO `creature_template` SELECT * FROM `bonk_cat`;
DROP TEMPORARY TABLE `bonk_cat`;

-- Cat: bites every 1.0 s (Call Pet's damage a second over 1.5x the bites),
-- 1 stack of Bleed (bonk_keyword 4) each.
INSERT INTO `bonk_pet_family` (`id`, `name`, `entry`, `scale`, `attack_ms`, `applies`, `apply_stacks`) VALUES
(1, 'Cat', 900114, 1, 1000, 4, 1);


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
