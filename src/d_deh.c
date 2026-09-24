//-----------------------------------------------------------------------------
//
// DESCRIPTION:
//    Minimal DeHackEd patch loader.
//
//    Added for Chex Quest: its original chex.exe was doom.exe with changes
//    that were never shipped in chex.wad itself - the level names, pickup
//    messages, finale text and a few monster/frame tweaks. Source ports
//    reproduce them with a community-made chex.deh (by Simon "fraggle"
//    Howard), which the build packs into the ROM as "dehacked.deh".
//
//    Only what that patch actually uses is implemented:
//      Thing N  - mobjinfo fields, by their DeHackEd names
//      Frame N  - state fields (not code pointers)
//      Text a b - string replacement: engine strings go through
//                 DEH_String() where they're displayed; sprite, sound and
//                 music names are renamed in place
//    Any other section (Cheat, Ammo, Weapon, Pointer, Sound, Misc, BEX
//    [STRINGS]...) is skipped with a message, so a patch that needs one
//    of them loads partially rather than failing. Cheat in particular is
//    deliberately ignored: the N64 has no keyboard, the Cheats menu types
//    the classic codes itself (see n64_do_cheat), and renaming them would
//    break that. Text replacements have no length limit (the patch's
//    "*allow-long-strings*" mode is always on).
//
//-----------------------------------------------------------------------------

#include <libdragon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "doomdef.h"
#include "i_system.h"
#include "info.h"
#include "sounds.h"
#include "d_deh.h"

typedef struct
{
    char *from;
    char *to;
} deh_text_t;

static deh_text_t *deh_texts;
static int deh_numtexts;
static int deh_maxtexts;

char *DEH_String(char *s)
{
    int i;

    if (s == NULL)
    {
        return s;
    }
    for (i = 0; i < deh_numtexts; i++)
    {
        if (!strcmp(deh_texts[i].from, s))
        {
            return deh_texts[i].to;
        }
    }
    return s;
}

static char *deh_strndup(const char *s, int len)
{
    char *d = malloc(len + 1);
    memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

// A Text replacement: renames a sprite, sound or music lump name in place
// when it matches one, otherwise remembers it for DEH_String().
static void DEH_AddText(const char *from, int fromlen, const char *to, int tolen)
{
    char *f = deh_strndup(from, fromlen);
    char *t = deh_strndup(to, tolen);
    int i;

    if (fromlen == 4 && tolen == 4)
    {
        for (i = 0; i < NUMSPRITES; i++)
        {
            if (!strcasecmp(sprnames[i], f))
            {
                sprnames[i] = t;
                free(f);
                return;
            }
        }
    }
    if (fromlen <= 6 && tolen <= 6)
    {
        for (i = 1; i < NUMSFX; i++)
        {
            if (S_sfx[i].name && !strcasecmp(S_sfx[i].name, f))
            {
                S_sfx[i].name = t;
                free(f);
                return;
            }
        }
        for (i = 1; i < NUMMUSIC; i++)
        {
            if (S_music[i].name && !strcasecmp(S_music[i].name, f))
            {
                S_music[i].name = t;
                free(f);
                return;
            }
        }
    }

    if (deh_numtexts == deh_maxtexts)
    {
        deh_maxtexts = deh_maxtexts ? deh_maxtexts * 2 : 64;
        deh_texts = realloc(deh_texts, deh_maxtexts * sizeof(*deh_texts));
    }
    deh_texts[deh_numtexts].from = f;
    deh_texts[deh_numtexts].to = t;
    deh_numtexts++;
}

static void DEH_SetThingField(mobjinfo_t *mi, const char *key, int v)
{
    if      (!strcasecmp(key, "ID #"))               mi->doomednum = v;
    else if (!strcasecmp(key, "Initial frame"))      mi->spawnstate = v;
    else if (!strcasecmp(key, "Hit points"))         mi->spawnhealth = v;
    else if (!strcasecmp(key, "First moving frame")) mi->seestate = v;
    else if (!strcasecmp(key, "Alert sound"))        mi->seesound = v;
    else if (!strcasecmp(key, "Reaction time"))      mi->reactiontime = v;
    else if (!strcasecmp(key, "Attack sound"))       mi->attacksound = v;
    else if (!strcasecmp(key, "Injury frame"))       mi->painstate = v;
    else if (!strcasecmp(key, "Pain chance"))        mi->painchance = v;
    else if (!strcasecmp(key, "Pain sound"))         mi->painsound = v;
    else if (!strcasecmp(key, "Close attack frame")) mi->meleestate = v;
    else if (!strcasecmp(key, "Far attack frame"))   mi->missilestate = v;
    else if (!strcasecmp(key, "Death frame"))        mi->deathstate = v;
    else if (!strcasecmp(key, "Exploding frame"))    mi->xdeathstate = v;
    else if (!strcasecmp(key, "Death sound"))        mi->deathsound = v;
    else if (!strcasecmp(key, "Speed"))              mi->speed = v;
    else if (!strcasecmp(key, "Width"))              mi->radius = v;
    else if (!strcasecmp(key, "Height"))             mi->height = v;
    else if (!strcasecmp(key, "Mass"))               mi->mass = v;
    else if (!strcasecmp(key, "Missile damage"))     mi->damage = v;
    else if (!strcasecmp(key, "Action sound"))       mi->activesound = v;
    else if (!strcasecmp(key, "Bits"))               mi->flags = v;
    else if (!strcasecmp(key, "Respawn frame"))      mi->raisestate = v;
    else printf("DEH: unknown Thing field '%s'\n", key);
}

static void DEH_SetFrameField(state_t *st, const char *key, int v)
{
    if      (!strcasecmp(key, "Sprite number"))    st->sprite = v;
    else if (!strcasecmp(key, "Sprite subnumber")) st->frame = v;
    else if (!strcasecmp(key, "Duration"))         st->tics = v;
    else if (!strcasecmp(key, "Next frame"))       st->nextstate = v;
    else if (!strcasecmp(key, "Unknown 1"))        st->misc1 = v;
    else if (!strcasecmp(key, "Unknown 2"))        st->misc2 = v;
    else printf("DEH: unsupported Frame field '%s'\n", key);
}

static char *DEH_Trim(char *s)
{
    char *e;

    while (*s && isspace((unsigned char)*s))
    {
        s++;
    }
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
    {
        *--e = '\0';
    }
    return s;
}

enum { SEC_NONE, SEC_THING, SEC_FRAME, SEC_SKIP };

void DEH_LoadFile(const char *path)
{
    int fd = dfs_open(path);
    if (fd < 0)
    {
        return;
    }

    int size = dfs_size(fd);
    char *buf = malloc(size + 1);
    if (buf == NULL || dfs_read(buf, 1, size, fd) != size)
    {
        I_Error("DEH_LoadFile: could not read %s", path);
    }
    dfs_close(fd);

    // Drop DOS line endings: Text lengths count a line break as one byte.
    int n = 0;
    for (int i = 0; i < size; i++)
    {
        if (buf[i] != '\r')
        {
            buf[n++] = buf[i];
        }
    }
    buf[n] = '\0';

    int section = SEC_NONE;
    int index = 0;
    char *p = buf;
    char *end = buf + n;

    while (p < end)
    {
        char *eol = memchr(p, '\n', end - p);
        char *next = eol ? eol + 1 : end;
        if (eol)
        {
            *eol = '\0';
        }
        char *line = DEH_Trim(p);
        p = next;

        if (line[0] == '\0' || line[0] == '#')
        {
            continue;
        }

        int a, b;
        if (sscanf(line, "Text %d %d", &a, &b) == 2)
        {
            // The two strings follow the header line directly, back to
            // back, measured in bytes - line breaks included.
            if (a < 0 || b < 0 || a + b > end - p)
            {
                printf("DEH: truncated Text block\n");
                break;
            }
            DEH_AddText(p, a, p + a, b);
            p += a + b;
            section = SEC_NONE;
            continue;
        }
        if (sscanf(line, "Thing %d", &a) == 1)
        {
            section = (a >= 1 && a <= NUMMOBJTYPES) ? SEC_THING : SEC_SKIP;
            index = a - 1;
            continue;
        }
        if (sscanf(line, "Frame %d", &a) == 1)
        {
            section = (a >= 0 && a < NUMSTATES) ? SEC_FRAME : SEC_SKIP;
            index = a;
            continue;
        }

        char *eq = strchr(line, '=');
        if (eq == NULL)
        {
            // Some other section header: not supported, skip its fields.
            if (strncasecmp(line, "Patch File", 10) != 0)
            {
                printf("DEH: skipping '%s'\n", line);
            }
            section = SEC_SKIP;
            continue;
        }

        *eq = '\0';
        char *key = DEH_Trim(line);
        int value = atoi(DEH_Trim(eq + 1));

        if (section == SEC_THING)
        {
            DEH_SetThingField(&mobjinfo[index], key, value);
        }
        else if (section == SEC_FRAME)
        {
            DEH_SetFrameField(&states[index], key, value);
        }
        // "Doom version = ", "Patch format = " and fields of skipped
        // sections land here with nothing to do.
    }

    free(buf);
    printf("DEH: loaded %s (%d text replacements)\n", path, deh_numtexts);
}
