DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012070000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012070000');
-- Add your query below.


-- Vanilla Bonk: no world interactables. Herbs, ore, chests, fishing pools,
-- quest objects, wanted posters, books, mailboxes and meeting stones never
-- spawn. Doors, levers, chairs, campfires, traps and transports stay, and so
-- do Vanilla Bonk's own objects (entries from 900000). The spawns turned off
-- here are listed in `bonk_hidden_gameobject`, so this can be undone.
CREATE TABLE IF NOT EXISTS `bonk_hidden_gameobject` (
    `guid` INT UNSIGNED NOT NULL,
    PRIMARY KEY (`guid`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8 COMMENT='Vanilla Bonk: world interactables turned off (gameobject.spawn_flags bit 2)';

-- Types: 2 quest giver, 3 chest (herbs, ore, containers), 9 text, 10 goober,
-- 13 camera, 18 summoning ritual, 19 mailbox, 20 auction house,
-- 23 meeting stone, 24 flag stand, 25 fishing hole.
INSERT IGNORE INTO `bonk_hidden_gameobject` (`guid`)
SELECT g.`guid`
FROM `gameobject` g
JOIN `gameobject_template` t ON t.`entry` = g.`id`
    AND t.`patch` = (SELECT MAX(t2.`patch`) FROM `gameobject_template` t2 WHERE t2.`entry` = g.`id`)
WHERE t.`type` IN (2, 3, 9, 10, 13, 18, 19, 20, 23, 24, 25)
  AND g.`id` < 900000
  AND (g.`spawn_flags` & 2) = 0;

UPDATE `gameobject` g
JOIN `bonk_hidden_gameobject` h ON h.`guid` = g.`guid`
SET g.`spawn_flags` = g.`spawn_flags` | 2;

-- To undo:
--   UPDATE `gameobject` g JOIN `bonk_hidden_gameobject` h ON h.`guid` = g.`guid`
--   SET g.`spawn_flags` = g.`spawn_flags` & ~2;
--   DELETE FROM `bonk_hidden_gameobject`;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
