// This file can be included several times.

#ifndef MACRO_CONFIG_INT
#error "The config macros must be defined"
#define MACRO_CONFIG_INT(Name, ScriptName, Def, Min, Max, Flags, Desc) ;
#define MACRO_CONFIG_COL(Name, ScriptName, Def, Flags, Desc) ;
#define MACRO_CONFIG_STR(Name, ScriptName, Len, Def, Flags, Desc) ;
#endif

MACRO_CONFIG_INT(BcTestCheckbox, bc_test_checkbox, 0, 0, 1, CFGFLAG_CLIENT | CFGFLAG_SAVE, "Test checkbox for BestClient settings")
