DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261011230000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261011230000');
-- Add your query below.


-- Vanilla Bonk: Talent Shards, the talent trainer's currency. They drop
-- rarely in runs (bosses and mini-bosses always, elites now and then) and
-- are picked up by walking over them.
ALTER TABLE `bonk_tier`
    ADD COLUMN `shard_chance_normal` FLOAT            NOT NULL DEFAULT 0.1 COMMENT '% chance per normal kill to drop a Talent Shard',
    ADD COLUMN `shard_chance_elite`  FLOAT            NOT NULL DEFAULT 12  COMMENT '% chance per elite kill',
    ADD COLUMN `shards_miniboss`     TINYINT UNSIGNED NOT NULL DEFAULT 1   COMMENT 'Talent Shards every mini-boss drops',
    ADD COLUMN `shards_boss`         TINYINT UNSIGNED NOT NULL DEFAULT 2   COMMENT 'Talent Shards every stage boss drops';

UPDATE `bonk_tier` SET `shard_chance_normal` = 0.1, `shard_chance_elite` = 12, `shards_miniboss` = 1, `shards_boss` = 2 WHERE `id` = 1;
UPDATE `bonk_tier` SET `shard_chance_normal` = 0.2, `shard_chance_elite` = 20, `shards_miniboss` = 2, `shards_boss` = 3 WHERE `id` = 2;

-- The dropped shard: an Un'Goro power crystal.
DELETE FROM `gameobject_template` WHERE `entry` = 900007;
INSERT INTO `gameobject_template` (`entry`, `patch`, `type`, `displayId`, `name`, `size`) VALUES
(900007, 0, 5, 2971, 'Talent Shard', 0.8);

-- Talents cost Talent Shards now: about 50 for all of them.
ALTER TABLE `bonk_talent`
    MODIFY COLUMN `cost` INT UNSIGNED NOT NULL DEFAULT 1 COMMENT 'Talent Shards for the first rank';

UPDATE `bonk_talent` SET `cost` = 3, `cost_growth` = 0   WHERE `id` = 1;  -- Hardened Foes: 3
UPDATE `bonk_talent` SET `cost` = 5, `cost_growth` = 0   WHERE `id` = 2;  -- Brutal Foes: 5
UPDATE `bonk_talent` SET `cost` = 3, `cost_growth` = 0   WHERE `id` = 3;  -- Swarming: 3
UPDATE `bonk_talent` SET `cost` = 5, `cost_growth` = 0   WHERE `id` = 4;  -- Champions: 5
UPDATE `bonk_talent` SET `cost` = 2, `cost_growth` = 0.5 WHERE `id` = 5;  -- Second Thoughts: 2, 3, 4
UPDATE `bonk_talent` SET `cost` = 2, `cost_growth` = 0.5 WHERE `id` = 6;  -- Banishment: 2, 3, 4
UPDATE `bonk_talent` SET `cost` = 8, `cost_growth` = 0   WHERE `id` = 7;  -- Second Wind: 8
UPDATE `bonk_talent` SET `cost` = 1, `cost_growth` = 0.5 WHERE `id` = 8;  -- Pocket Change: 1, 2, 2, 3

UPDATE `broadcast_text` SET
    `male_text`   = 'Learn a trick or two for the road, or invite more danger for more gold. I teach for Talent Shards: they turn up rarely out there, and elites and bosses carry more.',
    `female_text` = 'Learn a trick or two for the road, or invite more danger for more gold. I teach for Talent Shards: they turn up rarely out there, and elites and bosses carry more.'
WHERE `entry` = 900004;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
