#ifndef __I_INPUT_H
#define __I_INPUT_H

// Simulates typing a classic Doom cheat code (see the switch in i_input.c
// for the cheat id -> code string mapping) by posting synthetic keydown/
// keyup events for each of its letters, exactly as if typed on a keyboard.
void n64_do_cheat(int cheat);

// Whether I_Tactile() (see i_system.h) is allowed to drive the Rumble Pak.
// Toggled from the Control Settings menu ("Pulse: Yes/No" - see m_menu.c).
extern int rumble_enabled;

#endif // __I_INPUT_H
