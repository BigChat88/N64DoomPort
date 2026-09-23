// Emacs style mode select   -*- C++ -*- h
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//    Doom input handler for Nintendo 64, libdragon
//
//-----------------------------------------------------------------------------


#include <libdragon.h>
#include <stdlib.h>

#include "i_input.h"
#include "i_sound.h"

#include "doomstat.h"
#include "i_system.h"
#include "v_video.h"
#include "m_argv.h"
#include "d_main.h"

#include "doomdef.h"

void n64_do_cheat(int cheat);

static int GODDED;

// B doubles as a gameplay action (weapon change / use) and "back out of
// menu". Remembers which one the last press picked, so the matching release
// posts the same key(s) - deciding again from menuactive at release time
// could pick differently than at press time (the press may have just closed
// the menu), leaving a keydown with no matching keyup and a stuck key.
static int b_pressed_for_menu;

// A doubles as a gameplay action (use / fire) and "confirm/select" in
// menus - the latter needs KEY_ENTER, on top of the gameplay key, but
// unconditionally sending KEY_ENTER even outside menus was wrong: in-game,
// HU_Responder treats a bare KEY_ENTER as HU_MSGREFRESH (see hu_stuff.h)
// and just restarts the HUD message widget's timeout without changing its
// text, so every action-button press outside a menu re-displayed whatever
// pickup message had last been shown, even if the use itself did nothing.
// Same press/release symmetry reasoning as b_pressed_for_menu above.
static int a_pressed_for_menu;

// Toggled from the Control Settings menu (see m_menu.c); persisted like
// control_type (see m_misc.c's defaults[]).
int rumble_enabled = 1;

// 0 = not currently rumbling. Sets an absolute deadline (a get_ticks_ms()
// value) rather than a tic countdown, since I_GetEvent below runs every tic
// regardless of pause/menu state - the same reason input polling itself
// has to live here instead of somewhere gameplay-tick-gated.
static uint32_t rumble_off_time = 0;

// Called by P_DamageMobj (via i_system.h) whenever the console player takes
// damage - "tactile feedback" is vanilla Doom's name for this hook, but it
// was always a no-op stub even in the original DOS release (real "tactile
// mouse" hardware never shipped), so there's no established meaning for
// on/off/total to preserve. total is the intended vanilla duration in
// milliseconds (40 for a graze up to 240 for a maximum hit) - clamped to a
// short, snappy buzz rather than honoring it exactly, since a real Rumble
// Pak motor spinning up for a sustained 240ms feels sluggish, not punchy.
void I_Tactile(int on, int off, int total)
{
    int duration;

    (void)on;
    (void)off;

    if (!rumble_enabled)
        return;

    if (!joypad_get_rumble_supported(JOYPAD_PORT_1))
        return;

    duration = total;
    if (duration < 60)
        duration = 60;
    if (duration > 250)
        duration = 250;

    joypad_set_rumble_active(JOYPAD_PORT_1, true);
    rumble_off_time = get_ticks_ms() + duration;
}

// Turns the motor back off once its deadline passes. Called every tic from
// I_GetEvent (see below) so it keeps working even while paused/in a menu.
static void I_UpdateRumble(void)
{
    if (rumble_off_time && get_ticks_ms() >= rumble_off_time)
    {
        joypad_set_rumble_active(JOYPAD_PORT_1, false);
        rumble_off_time = 0;
    }
}

uint32_t current_map;
uint32_t current_episode;
GameMode_t current_mode;

static int pad_weapon = 1;
// index i is weapontype_t i (wp_fist..wp_chainsaw); the digit key that
// requests that weapon slot is simply '1' + i (see G_BuildTiccmd's
// "gamekeydown['1'+i]" loop). Do not introduce gaps/duplicates here - a
// repeated digit sends the exact same keypress as another slot, which
// looks like the weapon-change button did nothing on that press.
static char weapons[8] = { '1', '2', '3', '4', '5', '6', '7', '8' };
static int lz_count = 0;

// Type II's C-Left/C-Right each cycle pad_weapon and post a keydown for
// whatever slot it lands on; released_key() then needs to post the matching
// keyup for THAT slot, not whatever pad_weapon happens to be when the
// button is released. Since C-Left and C-Right are independent buttons,
// pressing one, then the other, then releasing the first would otherwise
// send the keyup for the second press's weapon instead - leaving the
// first press's digit key stuck down (a continuous, repeated weapon-switch
// request every tic thereafter, since gamekeydown[] is polled every tic).
static char pad_weapon_key_c_left;
static char pad_weapon_key_c_right;

// Same reasoning for Type I, where B (next weapon) and C-Down (previous
// weapon) are the two independent buttons that can cycle pad_weapon.
static char pad_weapon_key_b;
static char pad_weapon_key_c_down;

//
// find_owned_weapon
// Starting from `start`, steps through weapon slots in `direction` (+1/-1,
// wrapping) and returns the first one the player actually owns. Blindly
// advancing pad_weapon by one and sending that digit key is a no-op when the
// player doesn't own that slot (the engine just ignores the switch), so a
// single cycle button press could silently do nothing - the player then has
// to mash the button through every unowned slot to get back to a weapon
// they actually have. Skipping unowned slots here makes the button always
// change to a real weapon on every press.
//
// Slot 0 (fist) needs the same extra check: p_user.c's vanilla weapon-change
// handler redirects a request for wp_fist to wp_chainsaw instead whenever
// the chainsaw is owned and the player isn't already wielding it berserk
// (see its "newweapon == wp_fist && ... newweapon = wp_chainsaw" block) -
// real DOOM's key '1' has always meant "fist or chainsaw, whichever makes
// sense" for exactly this reason, not "fist specifically". Landing this
// cycle on slot 0 while already on the chainsaw (the common case: no
// berserk yet) sent that same redirect, which resolves right back to the
// chainsaw - a silent no-op the player reported as "changing weapon from
// the chainsaw always needs the button pressed twice": the first press
// picked slot 0, did nothing, and only the second press (landing on the
// next real slot) visibly changed anything. Compute the same effective
// weapon p_user.c would land on and skip slot 0 unless it would actually
// change something.
//
static int find_owned_weapon(int start, int direction)
{
    int i = start;
    player_t *p = &players[consoleplayer];

    for (int tries = 0; tries < 8; tries++)
    {
        i = (i + direction + 8) % 8;

        if (!p->weaponowned[i])
        {
            continue;
        }

        if (i == wp_fist)
        {
            int effective = wp_fist;
            if (p->weaponowned[wp_chainsaw]
                && !(p->readyweapon == wp_chainsaw && p->powers[pw_strength]))
            {
                effective = wp_chainsaw;
            }
            if (effective == p->readyweapon)
            {
                continue;
            }
        }

        return i;
    }

    return start;
}

int controller_mapping = 0;
extern int control_type; // 0 = Type I (analog stick move/turn, C-buttons strafe), 1 = Type II (D-pad move/turn, L/R strafe)
int last_x = 0;
int last_y = 0;
int center_x = 0;
int center_y = 0;

void pressed_key(joypad_buttons_t *p_data);
void held_key(joypad_inputs_t *h_data);
void released_key(joypad_buttons_t *r_data);

int current_player_for_input = 0;


//
// I_GetEvent
// called by I_StartTic, scans player 1 controller for keys
// held_key is only used to scan analog stick to handle "mouse" event
// pressed_key/released_key are used to handle "keyboard" events
//
void I_GetEvent(void)
{
    joypad_poll();

    I_UpdateRumble();

    joypad_buttons_t keys_pressed = joypad_get_buttons_pressed(JOYPAD_PORT_1);
    joypad_buttons_t keys_released = joypad_get_buttons_released(JOYPAD_PORT_1);
    joypad_inputs_t stick_state = joypad_get_inputs(JOYPAD_PORT_1);
    // one of these days...
    current_player_for_input = 0;
    pressed_key(&keys_pressed);//,0);
    held_key(&stick_state);//,0);
    released_key(&keys_released);//,0);
}


//
// I_StartTic
// just calls I_GetEvent
//
void I_StartTic(void)
{
    I_GetEvent();
}


// dead zone for the analog stick, out of the (-127,127) raw range
#define STICK_DEADZONE 32

// real N64 sticks rarely report much past this even at full physical
// deflection (the -128..127 range is nominal, not achievable in practice),
// so this is treated as "fully tilted" for scaling purposes
#define STICK_MAX_RAW 100

// turning rate at full stick deflection, in the same units as G_BuildTiccmd's
// mousex (it does "cmd->angleturn -= mousex*0x8"). angleturn[]'s fastest
// keyboard/joystick-button turn rate is 1280 (running + holding turn), which
// needs mousex of 160 - picked a bit higher than that so a full push turns
// at least as fast as the keyboard, which is the "still feels slow" the
// previous flat linear divide couldn't fix: dividing the ~68-unit usable
// range (100-32 deadzone) by even 2 topped out around mousex 34 (angleturn
// ~270), well under even the slow-turn rate of 320.
#define STICK_TURN_MAX_TYPE1 200
// Type II frees the stick from strafing duty (L/R handle that instead), so
// it's used purely for turning there and can afford to be a bit punchier.
#define STICK_TURN_MAX_TYPE2 260

//
// stick_turn_amount
// Maps raw stick deflection (past the deadzone) to a turn rate using a
// squared response curve instead of a flat linear scale: small pushes stay
// slow/precise (gentle gradient near center) while the rate ramps up
// quickly as the stick approaches full deflection, reaching turn_max right
// at the edge. This is the "gradient" feel modern analog-stick controls use,
// as opposed to a linear divide which is either too twitchy near center or
// too sluggish everywhere once toned down to fix that.
//
static int stick_turn_amount(int deflection, int turn_max)
{
    int mag = abs(deflection) - STICK_DEADZONE;

    if (mag <= 0)
    {
        return 0;
    }

    int range = STICK_MAX_RAW - STICK_DEADZONE;

    if (mag > range)
    {
        mag = range;
    }

    int turn = (mag * mag * turn_max) / (range * range);

    return (deflection < 0) ? -turn : turn;
}

//
// held_key
// this function maps analog stick position into input events: digital
// forward/back (like the D-Pad) plus analog left/right turning.
//
// Previously the whole stick was fed in as raw "mouse" movement, which is
// added straight onto the player's speed every tic (see G_BuildTiccmd's
// "forward += mousey"). That bypasses the walk/run distinction entirely, so
// even a light tap of the stick moved the player at (or above) running
// speed. Forward/back is now posted as a digital ev_joystick event instead,
// routing it through the same code path as the D-Pad (joyymove), which
// moves at walk speed unless the run key (C-Up) is held.
//
// Turning is kept analog (posted as an ev_mouse event, consumed only for
// angleturn - see G_BuildTiccmd's "cmd->angleturn -= mousex*0x8") rather
// than digital, since a fixed per-tic turn rate felt too fast/snappy on a
// gamepad; dividing down the raw deflection lets a light push turn slowly
// while still allowing a fast turn with a hard push.
//
void held_key(joypad_inputs_t *h_data) //, int player)
{
    event_t ev;

    last_x = h_data->stick_x - center_x;
    last_y = h_data->stick_y - center_y;

    // forward/back (digital, walk speed only)
    ev.type = ev_joystick;
    ev.data1 = 0;
    ev.data2 = 0; // turning is handled by the analog mouse event below instead
    // inverted: G_BuildTiccmd treats joyymove < 0 as forward
    ev.data3 = (last_y > STICK_DEADZONE) ? -1 : (last_y < -STICK_DEADZONE) ? 1 : 0;
    D_PostEvent(&ev);

    // left/right turning (analog)
    int turn_max = (control_type == 1) ? STICK_TURN_MAX_TYPE2 : STICK_TURN_MAX_TYPE1;
    ev.type = ev_mouse;
    ev.data1 = 0;
    ev.data2 = stick_turn_amount(last_x, turn_max);
    ev.data3 = 0; // no forward contribution here - handled by the joystick event above
    D_PostEvent(&ev);
}

//
// pressed_key
// handle pressed buttons that are mapped to keyboard event operations such as
// moving, shooting, opening doors, toggling run mode
// also handles out-of-band input operations like toggling GOD MODE, idclev
//
void pressed_key(joypad_buttons_t *p_data) //, int player)
{
    event_t doom_input_event;
    joypad_buttons_t pressed = *p_data;

    if (control_type == 0)
    {
#if 1
        // CHEAT WARP TO NEXT LEVEL
        if (pressed.l && pressed.z && !pressed.r)
        {
            if ( (lz_count > 0) && (lz_count % 4 == 0) )
            {
                n64_do_cheat(13); // IDCLEV
            }

            lz_count += 1;
        }

        // TOGGLE GOD MODE
        if (pressed.l && pressed.r && !pressed.z)
        {
            if (!GODDED)
            {
                n64_do_cheat(1);  // IDDQD
                n64_do_cheat(3);  // IDKFA
                n64_do_cheat(10); // IDBEHOLDA
                n64_do_cheat(5);  // IDDT
            }

            GODDED ^= 1;
        }
#endif

        // FIRE (guarded so the L+Z warp cheat doesn't also fire a shot)
        if (pressed.z && !pressed.l)
        {
            doom_input_event.data1 = KEY_RCTRL;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }

        // ACTION/USE - also confirms/selects in menus
        if (pressed.a)
        {
            a_pressed_for_menu = menuactive;

            doom_input_event.data1 = ' ';
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);

            if (menuactive)
            {
                doom_input_event.data1 = KEY_ENTER;
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
        }

        // CHANGE WEAPON (next) - also backs out one menu level
        if (pressed.b)
        {
            b_pressed_for_menu = menuactive;

            if (menuactive)
            {
                doom_input_event.data1 = KEY_BACKSPACE;
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
            else
            {
                pad_weapon = find_owned_weapon(pad_weapon, 1);
                pad_weapon_key_b = weapons[pad_weapon];

                doom_input_event.data1 = pad_weapon_key_b;
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
        }

        // CHANGE WEAPON (previous) - B only goes forward, so this is the
        // only way to step back to the weapon before the current one
        if (pressed.c_down)
        {
            pad_weapon = find_owned_weapon(pad_weapon, -1);
            pad_weapon_key_c_down = weapons[pad_weapon];

            doom_input_event.data1 = pad_weapon_key_c_down;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }

        // STRAFE LEFT/RIGHT
        if (pressed.c_left && !pressed.c_right)
        {
            doom_input_event.data1 = ',';
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }
        if (pressed.c_right && !pressed.c_left)
        {
            doom_input_event.data1 = '.';
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }

        // AUTOMAP (guarded so the L+R god mode cheat doesn't also toggle it)
        if (pressed.r && !pressed.l)
        {
            doom_input_event.data1 = KEY_TAB;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }
    }
    else // control_type == 1: Type II
    {
        // FIRE - also confirms/selects in menus
        if (pressed.a)
        {
            a_pressed_for_menu = menuactive;

            doom_input_event.data1 = KEY_RCTRL;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);

            if (menuactive)
            {
                doom_input_event.data1 = KEY_ENTER;
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
        }

        // OPEN DOORS/USE - also backs out one menu level
        if (pressed.b)
        {
            b_pressed_for_menu = menuactive;

            if (menuactive)
            {
                doom_input_event.data1 = KEY_BACKSPACE;
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
            else
            {
                doom_input_event.data1 = ' ';
                doom_input_event.type = ev_keydown;
                D_PostEvent(&doom_input_event);
            }
        }

        // STRAFE LEFT/RIGHT (lateral movement)
        if (pressed.l && !pressed.r)
        {
            doom_input_event.data1 = ',';
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }
        if (pressed.r && !pressed.l)
        {
            doom_input_event.data1 = '.';
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }

        // AUTOMAP (C-Up is RUN, shared below)
        if (pressed.c_down)
        {
            doom_input_event.data1 = KEY_TAB;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }

        // CHANGE WEAPON - previous/next
        if (pressed.c_left)
        {
            pad_weapon = find_owned_weapon(pad_weapon, -1);
            pad_weapon_key_c_left = weapons[pad_weapon];

            doom_input_event.data1 = pad_weapon_key_c_left;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }
        if (pressed.c_right)
        {
            pad_weapon = find_owned_weapon(pad_weapon, 1);
            pad_weapon_key_c_right = weapons[pad_weapon];

            doom_input_event.data1 = pad_weapon_key_c_right;
            doom_input_event.type = ev_keydown;
            D_PostEvent(&doom_input_event);
        }
    }

    // shared between both control types: C-Up run (held), D-Pad
    // movement/menu navigation and Start
    // RUN - KEY_RSHIFT is key_speed's default binding (see m_misc.c)
    if (pressed.c_up)
    {
        doom_input_event.data1 = KEY_RSHIFT;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
    if (pressed.d_up)
    {
        doom_input_event.data1 = KEY_UPARROW;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
    if (pressed.d_down)
    {
        doom_input_event.data1 = KEY_DOWNARROW;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
    if (pressed.d_left)
    {
        doom_input_event.data1 = KEY_LEFTARROW;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
    if (pressed.d_right)
    {
        doom_input_event.data1 = KEY_RIGHTARROW;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
#if AUDIO_DEBUG
    // Diagnostic build: L+Start cycles the audio test modes (see i_sound.c)
    // instead of opening the menu.
    if (pressed.start && joypad_get_buttons_held(JOYPAD_PORT_1).l)
    {
        I_AudioDebugCycle();
    }
    else
#endif
    if (pressed.start)
    {
        doom_input_event.data1 = KEY_ESCAPE;
        doom_input_event.type = ev_keydown;
        D_PostEvent(&doom_input_event);
    }
}


//
// released_key
// handle released buttons that are mapped to keyboard event operations such as
// moving, shooting, opening doors, etc
//
void released_key(joypad_buttons_t *r_data) //, int player)
{
    event_t doom_input_event;

    joypad_buttons_t released = *r_data;

    if (control_type == 0)
    {
        if (released.z)
        {
            doom_input_event.data1 = KEY_RCTRL;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.a)
        {
            doom_input_event.data1 = ' ';
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);

            if (a_pressed_for_menu)
            {
                doom_input_event.data1 = KEY_ENTER;
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
        }
        if (released.b)
        {
            if (b_pressed_for_menu)
            {
                doom_input_event.data1 = KEY_BACKSPACE;
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
            else
            {
                doom_input_event.data1 = pad_weapon_key_b;
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
        }
        if (released.c_down)
        {
            doom_input_event.data1 = pad_weapon_key_c_down;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.c_left && !released.c_right)
        {
            doom_input_event.data1 = ',';
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.c_right && !released.c_left)
        {
            doom_input_event.data1 = '.';
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.r)
        {
            doom_input_event.data1 = KEY_TAB;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
    }
    else // control_type == 1: Type II
    {
        if (released.a)
        {
            doom_input_event.data1 = KEY_RCTRL;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);

            if (a_pressed_for_menu)
            {
                doom_input_event.data1 = KEY_ENTER;
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
        }
        if (released.b)
        {
            if (b_pressed_for_menu)
            {
                doom_input_event.data1 = KEY_BACKSPACE;
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
            else
            {
                doom_input_event.data1 = ' ';
                doom_input_event.type = ev_keyup;
                D_PostEvent(&doom_input_event);
            }
        }
        if (released.l && !released.r)
        {
            doom_input_event.data1 = ',';
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.r && !released.l)
        {
            doom_input_event.data1 = '.';
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.c_down)
        {
            doom_input_event.data1 = KEY_TAB;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.c_left)
        {
            doom_input_event.data1 = pad_weapon_key_c_left;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
        if (released.c_right)
        {
            doom_input_event.data1 = pad_weapon_key_c_right;
            doom_input_event.type = ev_keyup;
            D_PostEvent(&doom_input_event);
        }
    }

    // shared between both control types: C-Up run, D-Pad movement/menu
    // navigation and Start
    if (released.c_up)
    {
        doom_input_event.data1 = KEY_RSHIFT;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
    if (released.d_up)
    {
        doom_input_event.data1 = KEY_UPARROW;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
    if (released.d_down)
    {
        doom_input_event.data1 = KEY_DOWNARROW;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
    if (released.d_left)
    {
        doom_input_event.data1 = KEY_LEFTARROW;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
    if (released.d_right)
    {
        doom_input_event.data1 = KEY_RIGHTARROW;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
    if (released.start)
    {
        doom_input_event.data1 = KEY_ESCAPE;
        doom_input_event.type = ev_keyup;
        D_PostEvent(&doom_input_event);
    }
}

static char __attribute__((aligned(8))) clevstr[9];

extern    char*    get_GAMEID();
extern    char*    doom1wad;
extern    char*    doomwad;
extern    char*    doomuwad;
extern    char*    doom2wad;
extern    char*    plutoniawad;
extern    char*    tntwad;

#define stricmp strcasecmp

void n64_do_cheat(int cheat)
{
    char       *str;
    int        i;
    event_t    event;

    switch (cheat)
    {
        case 1: // God Mode
        {
            str = "iddqd";
            break;
        }
        case 2: // Fucking Arsenal
        {
            str = "idfa";
            break;
        }
        case 3: // Key Full Ammo
        {
            str = "idkfa";
            break;
        }
        case 4: // No Clipping
        {
            str = "idclip";
            break;
        }
        case 5: // Toggle Map
        {
            str = "iddt";
            break;
        }
        case 6: // Invincible with Chainsaw
        {
            str = "idchoppers";
            break;
        }
        case 7: // Berserker Strength Power-up
        {
            str = "idbeholds";
            break;
        }
        case 8: // Invincibility Power-up
        {
            str = "idbeholdv";
            break;
        }
        case 9: // Invisibility Power-Up
        {
            str = "idbeholdi";
            break;
        }
        case 10: // Automap Power-up
        {
            str = "idbeholda";
            break;
        }
        case 11: // Anti-Radiation Suit Power-up
        {
            str = "idbeholdr";
            break;
        }
        case 12: // Light-Amplification Visor Power-up
        {
            str = "idbeholdl";
            break;
        }
        case 13: // change level
        {
            const char *gameid = get_GAMEID();

            if (!stricmp(doom2wad,gameid) || !stricmp(tntwad,gameid) || !stricmp(plutoniawad,gameid))
            {
                if (!stricmp(doom2wad,gameid))
                {
                    if (current_map < 32)
                    {
                        current_map++;
                    }
                    else
                    {
                        current_map = 1;
                    }
                }
                else
                {
                    // TNT.WAD and PLUTONIA.WAD both max out at MAP32, same
                    // as DOOM2.WAD above - this used to wrap at 34, warping
                    // to two maps (33/34) that don't exist in either WAD.
                    if(current_map < 32) {
                        current_map++;
                    }
                    else
                    {
                        current_map = 1;
                    }
                }

                sprintf(clevstr, "idclev%02ld", current_map);

                str = clevstr;
            }
            else
            {
                // shut up compiler...
                if (current_episode > 4)
                {
                    I_Error("n64_do_cheat: Invalid current_episode: %ld.", current_episode);
                    return;
                }

                if (current_map < 9)
                {
                    current_map++;
                }
                else
                {
                    current_map = 1;
                }

                sprintf(clevstr, "idclev%1ld%1ld", current_episode, current_map);
                str = clevstr;
            }
            break;
        }
        default:
        {
            return;
        }
    }

    for (i=0; i<strlen(str); i++)
    {
        event.type = ev_keydown;
        event.data1 = str[i];
        D_PostEvent (&event);
        event.type = ev_keyup;
        event.data1 = str[i];
        D_PostEvent (&event);
    }
}

