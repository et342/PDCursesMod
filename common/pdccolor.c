#ifdef NO_STDINT_H
   #define uint64_t unsigned long long
   #define uint32_t unsigned long
   #define uint16_t unsigned short
#else
   #include <stdint.h>
#endif
   #include <stdlib.h>

#define PACKED_RGB uint32_t

#ifndef PACK_RGB
   #define PACK_RGB( red, green, blue) ((red) | ((green)<<8) | ((blue) << 16))
#endif

#include <curspriv.h>
#include "pdccolor.h"

int PDC_blink_state = 0;
int PDC_really_blinking = FALSE;

static PACKED_RGB *PDC_rgbs;

int PDC_init_palette( void)
{
    int i, r, g, b;

    PDC_rgbs = (PACKED_RGB *)calloc( COLORS, sizeof( PACKED_RGB));
    if( !PDC_rgbs)
        return( -1);
    for( i = 0; i < 16 && i < COLORS; i++)
    {
        const int intensity = ((i & 8) ? 0xff : 0xc0);

        PDC_rgbs[i] = PACK_RGB( ((i & COLOR_RED) ? intensity : 0),
                           ((i & COLOR_GREEN) ? intensity : 0),
                           ((i & COLOR_BLUE) ? intensity : 0));
    }
           /* 256-color xterm extended palette:  216 colors in a
            6x6x6 color cube,  plus 24 (not 50) shades of gray */
    for( r = 0; r < 6; r++)
        for( g = 0; g < 6; g++)
            for( b = 0; b < 6; b++)
                if( i < COLORS)
                    PDC_rgbs[i++] = PACK_RGB( r ? r * 40 + 55 : 0,
                                   g ? g * 40 + 55 : 0,
                                   b ? b * 40 + 55 : 0);
    for( i = 0; i < 24; i++)
        if( i + 232 < COLORS)
            PDC_rgbs[i + 232] = PACK_RGB( i * 10 + 8, i * 10 + 8, i * 10 + 8);
    return( 0);
}

void PDC_free_palette( void)
{
   if( PDC_rgbs)
      free( PDC_rgbs);
   PDC_rgbs = NULL;
}

PACKED_RGB PDC_get_palette_entry( const int idx)
{
   PACKED_RGB rval;

   if( !PDC_rgbs && PDC_init_palette( ))
      rval = ( idx ? 0xffffff : 0);
   else
      rval = PDC_rgbs[idx];
   return( rval);
}

/* Return value is -1 if no palette could be allocated,  0 if the color
didn't change (new RGB matched the old one),  and 1 if the color changed. */

int PDC_set_palette_entry( const int idx, const PACKED_RGB rgb)
{
   int rval;

   if( !PDC_rgbs && PDC_init_palette( ))
      rval = -1;
   else
      {
      rval = (PDC_rgbs[idx] == rgb ? 1 : 0);
      PDC_rgbs[idx] = rgb;
      }
   return( rval);
}

    /* This function 'intensifies' a color by shifting it toward white. */
    /* It used to average the input color with white.  Then it did a    */
    /* weighted average:  2/3 of the input color,  1/3 white,   for a   */
    /* lower "intensification" level.                                   */
    /*    Then Mark Hessling suggested that the output level should     */
    /* remap zero to 85 (= 255 / 3, so one-third intensity),  and input */
    /* of 192 or greater should be remapped to 255 (full intensity).    */
    /* Assuming we want a linear response between zero and 192,  that   */
    /* leads to output = 85 + input * (255-85)/192.                     */
    /*    This should lead to proper handling of bold text in legacy    */
    /* apps,  where "bold" means "high intensity".                      */

static PACKED_RGB intensified_color( PACKED_RGB ival)
{
    int rgb, i;
    PACKED_RGB oval = 0;

    for( i = 0; i < 3; i++, ival >>= 8)
    {
        rgb = (int)( ival & 0xff);
        if( rgb >= 192)
            rgb = 255;
        else
            rgb = 85 + rgb * (255 - 85) / 192;
        oval |= ((PACKED_RGB)rgb << (i * 8));
    }
    return( oval);
}

   /* For use in adjusting colors for A_DIMmed characters.  Just */
   /* knocks down the intensity of R, G, and B by 1/3.           */

static PACKED_RGB dimmed_color( PACKED_RGB ival)
{
    unsigned i;
    PACKED_RGB oval = 0;

    for( i = 0; i < 3; i++, ival >>= 8)
    {
        unsigned rgb = (unsigned)( ival & 0xff);

        rgb -= (rgb / 3);
        oval |= ((PACKED_RGB)rgb << (i * 8));
    }
    return( oval);
}


            /* PDCurses stores RGBs in fifteen bits,  five bits each */
            /* for red, green, blue.  A PACKED_RGB uses eight bits per */
            /* channel.  Hence the following.                        */
#if defined( CHTYPE_LONG) && CHTYPE_LONG >= 2
static PACKED_RGB extract_packed_rgb( const chtype color)
{
    const int red   = (int)( (color << 3) & 0xf8);
    const int green = (int)( (color >> 2) & 0xf8);
    const int blue  = (int)( (color >> 7) & 0xf8);

    return( PACK_RGB( red, green, blue));
}
#endif

void PDC_get_rgb_values( const chtype srcp,
            PACKED_RGB *foreground_rgb, PACKED_RGB *background_rgb)
{
    const int color = (int)(( srcp & A_COLOR) >> PDC_COLOR_SHIFT);
    bool reverse_colors = ((srcp & A_REVERSE) ? TRUE : FALSE);
    bool intensify_backgnd = FALSE;

#if defined( CHTYPE_LONG) && CHTYPE_LONG >= 2
    if( srcp & A_RGB_COLOR)
    {
        /* Extract RGB from 30 bits of the color field */
        *background_rgb = extract_packed_rgb( srcp >> PDC_COLOR_SHIFT);
        *foreground_rgb = extract_packed_rgb( srcp >> (PDC_COLOR_SHIFT + 15));
    }
    else
#endif
    {
        short foreground_index, background_index;

        PDC_pair_content( (short)color, &foreground_index, &background_index);
        *foreground_rgb = PDC_rgbs[foreground_index];
        *background_rgb = PDC_rgbs[background_index];
    }

    if( srcp & A_BLINK)
    {
        if( !PDC_really_blinking)   /* convert 'blinking' to 'bold' */
            intensify_backgnd = TRUE;
        else if( PDC_blink_state)
            reverse_colors ^= 1;
    }
    if( reverse_colors)
    {
        const PACKED_RGB temp = *foreground_rgb;

        *foreground_rgb = *background_rgb;
        *background_rgb = temp;
    }

    if( srcp & A_BOLD)
        *foreground_rgb = intensified_color( *foreground_rgb);
    if( intensify_backgnd)
        *background_rgb = intensified_color( *background_rgb);
    if( srcp & A_DIM)
    {
        *foreground_rgb = dimmed_color( *foreground_rgb);
        *background_rgb = dimmed_color( *background_rgb);
    }
}