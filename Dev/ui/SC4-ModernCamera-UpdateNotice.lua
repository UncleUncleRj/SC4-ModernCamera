--#-package:3d0c0a01# -- package signature --
-- News ticker notice for a newer SC4-ModernCamera release. The plugin sets the
-- two sc4moderncamera fields once it has compared the latest GitHub release
-- with its own version; until then the trigger stays false.

dofile("_adv_sys.lua")
dofile("_adv_advice.lua")
dofile("adv_game_data.lua")

sc4moderncamera = { update_available = 0, update_version = "" }

local a = advices : create_advice(hex2dec("3d0c0a02"), nil)
a.type = advice_types . FLUFF_NEWS
a.mood = advice_moods . NEUTRAL
-- The ticker skips anything under 59 while any active advice is 60 or higher, and the
-- lowest popup threshold is ADVICE_PRIORITY_MED_HIGH (60), so 59 is seen but never pops up.
a.priority = 59
a.trigger = "sc4moderncamera.update_available == 1"
-- LTEXT resources from tools/build_sc4_ui_dat.py; the game requires keys, not literal text.
a.title = "text@3d0c0a03"
a.message = "text@3d0c0a04"
