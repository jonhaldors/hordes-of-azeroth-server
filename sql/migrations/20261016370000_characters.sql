DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261016370000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261016370000');
-- Add your query below.


-- Vanilla Bonk, Phase 4 codex: the bestiary (kills per enemy and the first)
-- and the pool toggle (unlocked spells, passives, cards and Glyphs switched
-- off; type as bonk_quest.reward_type: 2 spell, 3 passive, 11 card, 12 Glyph).
CREATE TABLE IF NOT EXISTS `bonk_account_bestiary` (
    `account`    INT UNSIGNED NOT NULL,
    `entry`      INT UNSIGNED NOT NULL COMMENT 'creature_template.entry',
    `kills`      INT UNSIGNED NOT NULL DEFAULT 0,
    `first_kill` BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT 'unix time',
    PRIMARY KEY (`account`, `entry`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Vanilla Bonk bestiary';

CREATE TABLE IF NOT EXISTS `bonk_account_disabled` (
    `account` INT UNSIGNED NOT NULL,
    `type`    TINYINT UNSIGNED NOT NULL,
    `id`      INT UNSIGNED NOT NULL,
    PRIMARY KEY (`account`, `type`, `id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb3 COMMENT='Vanilla Bonk pool toggle: switched off';


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
