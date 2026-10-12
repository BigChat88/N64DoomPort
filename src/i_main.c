// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// $Log:$
//
// DESCRIPTION:
//    Main program, simply calls D_DoomMain high level loop.
//
//-----------------------------------------------------------------------------
#include <limits.h>

#include <stdlib.h>

#include <inttypes.h>
#include <errno.h>
#include <libdragon.h>

#include "doomdef.h"
#include "m_argv.h"
#include "d_main.h"
#include "g_game.h"
#include "i_intro.h"

extern void DoomIsOver();
// Controller Pak free space, through cpakfs - see g_game.c
extern int pak_free_blocks(void);

void check_and_init_mempak(void)
{
    switch (joypad_get_accessory_type(JOYPAD_PORT_1))
    {
        case JOYPAD_ACCESSORY_TYPE_NONE:
        {
            printf("No accessory inserted!\n");
            break;
        }
        case JOYPAD_ACCESSORY_TYPE_CONTROLLER_PAK:
        {
            // Through cpakfs, like the saves themselves (see g_game.c).
            // Only a pak with no filesystem at all - cpakfs_mount failing
            // with ENODEV: no valid ID sector, i.e. never formatted - gets
            // formatted. The old mempak check formatted on *any* problem
            // validate_mempak reported, which includes minor, recoverable
            // filesystem issues: that would have wiped every other game's
            // saves on the pak. Anything else is left alone; saving just
            // reports the pak as bad.
            if (cpakfs_mount(JOYPAD_PORT_1, "bootpak:/") == 0)
            {
                cpakfs_unmount(JOYPAD_PORT_1);
                printf("\nFree space: %d blocks\n", pak_free_blocks());
            }
            else if (errno == ENODEV)
            {
                printf("Controller Pak is not formatted! Formatting it.\n");
                if (cpakfs_format(JOYPAD_PORT_1, false) < 0)
                {
                    printf("Error formatting Controller Pak!\n");
                }
                else
                {
                    printf("Controller Pak formatted!\n");
                }
            }
            else
            {
                printf("Controller Pak unreadable or damaged - not touching it.\n");
            }

            break;
        }
        case JOYPAD_ACCESSORY_TYPE_RUMBLE_PAK:
        {
            printf("Cannot read data off of rumblepak!\n");
            break;
        }
        default:
        {
            break;
        }
    }
}


extern int center_x, center_y;

int main(int argc, char **argv)
{
//    console_init();
//debug_init_isviewer();

//    console_set_render_mode(RENDER_AUTOMATIC);
    if (dfs_init( DFS_DEFAULT_LOCATION ) != DFS_ESUCCESS)
    {
        printf("Could not initialize filesystem!\n");
        while(1);
    }
    joypad_init();

    I_DragonIntro();

    // center joystick...
    joypad_poll();
    joypad_inputs_t inputs = joypad_get_inputs(JOYPAD_PORT_1);
    center_x = inputs.stick_x;
    center_y = inputs.stick_y;

    printf("64Doom by jnmartin84\n");
    printf("github.com/jnmartin84/64doom/\n");
    printf("built %s %s\n", __DATE__, __TIME__);

    int available_memory_size = get_memory_size();
    //printf("Available memory: %d bytes\n", *(int *)(0x80000318));

    if(available_memory_size != 0x800000)
    {
        printf("\n***********************************");
        printf("Expansion Pak not found.\n");
        printf("It is required to run 64Doom.\n");
        printf("Please turn off the Nintendo 64,\ninstall Expansion Pak,\nand try again.\n");
        printf("***********************************\n");
        while(1) {}
    }

    //printf("Expansion Pak found.\n");

    printf("Checking for Mempak:\n");
    check_and_init_mempak();

    D_DoomMain();
    DoomIsOver();

    return 0;
}
