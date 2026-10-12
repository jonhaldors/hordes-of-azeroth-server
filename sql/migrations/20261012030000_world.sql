DROP PROCEDURE IF EXISTS add_migration;
DELIMITER ??
CREATE PROCEDURE `add_migration`()
BEGIN
DECLARE v INT DEFAULT 1;
SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='20261012030000');
IF v = 0 THEN
INSERT INTO `migrations` VALUES ('20261012030000');
-- Add your query below.


-- =============================================================
-- Vanilla Bonk: quest marker over the quest board NPC
--
-- Marla Ledgerwick is a quest giver as well as a gossip NPC, so the client
-- asks for her quest status; her script always answers "available" and
-- the yellow "!" shows above her head.
-- =============================================================

UPDATE `creature_template` SET `npc_flags` = 3 WHERE `entry` = 900005;


-- End of migration.
END IF;
END??
DELIMITER ;
CALL add_migration();
DROP PROCEDURE IF EXISTS add_migration;
