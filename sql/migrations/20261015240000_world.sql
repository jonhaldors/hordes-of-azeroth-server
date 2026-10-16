DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261015240000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261015240000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: Serpent Sting puts 3 stacks of Poison on each enemy it
-- stings (user-approved, Oct 15). A stack stays 0.1 of a grunt's health a
-- second; at one stack a sting, its targets died to other shots long before
-- the poison came to anything.
-- =============================================================

UPDATE `bonk_weapon` SET `apply_stacks` = 3,
    `description` = 'Stings the nearest enemy and 2 enemies near it, applying 3 stacks of Poison to each.'
WHERE `id` = 38;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
